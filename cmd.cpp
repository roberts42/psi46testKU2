/* -------------------------------------------------------------
 *
 *  file:        command.cpp
 *
 *  description: command line interpreter for Chip/Wafer tester
 *
 *  author:      Beat Meier
 *  modified:    31.8.2007
 *
 *  rev:
 *
 * -------------------------------------------------------------
 */


#include <math.h>
#include <time.h>
#include <fstream>
#include <utility>
#include "psi46test.h"
#include "plot.h"
#include "analyzer.h"

#include "command.h"
#include "defectlist.h"
#include "rpc.h"
#include <iostream>
#include <iomanip>
#include "TH2I.h"
#include "TCanvas.h"
#include "TFile.h"
#include <sstream>

using namespace std;


#ifndef _WIN32
#define _int64 long long
#endif

#define DO_FLUSH  if (par.isInteractive()) tb.Flush();

using namespace std;

#define FIFOSIZE 8192

//        	cable length:     5   48  prober 450 cm
extern int delayAdjust =  4; //  4    0    19    5
extern int deserAdjust =  4; //  4    4     5    6

void HexToBin(uint16_t hex_number, char * bit_number) {
        int max = 0x8000;
        for(int i = 0 ; i <16 ; i++){
            bit_number [i] = (hex_number & max ) ? '1' : '0';

            max >>=1;
    }
        bit_number[16] = 0;
}
void HexToBin32(uint32_t hex_number, char * bit_number) {

        uint32_t max = 0x80000000;

        for(int i = 0 ; i <32 ; i++){
            bit_number [i] = (hex_number & max ) ? '1' : '0';

            max >>=1;
    }
        bit_number[32] = 0;
}

int ParseData( std::vector<uint16_t> & data, int tbmch, std::vectorR<int> & iroc, std::vectorR<int> & row, std::vectorR<int> & col, std::vectorR<int> & ph);



// =======================================================================
//  connection, communication, startup commands
// =======================================================================

CMD_PROC(scan)
{
	CTestboard *tb = new CTestboard;
	string name;
	vector<string> devList;
	unsigned int nDev, nr;

	try
	{
		if (!tb->EnumFirst(nDev)) throw int(1);
		for (nr=0; nr<nDev; nr++)
		{
			if (!tb->EnumNext(name)) throw int(2);
			if (name.size() < 4) continue;
			if (name.compare(0, 4, "DTB_") == 0) devList.push_back(name);
		}
	}
	catch (int e)
	{
		switch (e)
		{
		case 1: printf("Cannot access the USB driver\n"); break;
		case 2: printf("Cannot read name of connected device\n"); break;
		}
		delete tb;
		return true;
	}

	if (devList.size() == 0)
	{
		printf("no DTB connected\n");
		return true;
	}

	for (nr = 0; nr < devList.size(); nr++)
	try
	{
		printf("%10s: ", devList[nr].c_str());
		if (!tb->Open(devList[nr],false))
		{
			printf("DTB in use\n");
			continue;
		}

		unsigned int bid = tb->GetBoardId();
		printf("DTB Id %u\n", bid);
		tb->Close();
	}
	catch (...)
	{
		printf("DTB not identifiable\n");
		tb->Close();
	}

	delete tb;

	return true;
}

CMD_PROC(open)
{
	if (tb.IsConnected())
	{
		printf("Already connected to DTB.\n");
		return true;
	}

	string usbId;
	char name[80];
	if (PAR_IS_STRING(name,79)) usbId = name;
	else if (!tb.FindDTB(usbId)) return true;

	bool status = tb.Open(usbId, false);

	if (!status)
	{
		printf("USB error: %s\nCould not connect to DTB %s\n", tb.ConnectionError(), usbId.c_str());
		return true;
	}
	printf("DTB %s opened\n", usbId.c_str());

	string info;
	tb.GetInfo(info);
	printf("--- DTB info-------------------------------------\n"
		   "%s"
		   "-------------------------------------------------\n", info.c_str());

	return true;
}

CMD_PROC(close)
{
	tb.Close();
	return true;
}

CMD_PROC(welcome)
{
	tb.Welcome();
	DO_FLUSH
	return true;
}

CMD_PROC(setled)
{
	int value;
	PAR_INT(value, 0, 0x3f);
	tb.SetLed(value);
	DO_FLUSH
	return true;
}

CMD_PROC(log)
{
	char s[256];
	PAR_STRINGEOL(s,255);
	Log.printf("%s\n", s);
	return true;
}

bool UpdateDTB(const char *filename)
{
	fstream src;

	if (tb.UpgradeGetVersion() == 0x0100)
	{
		// open file
		src.open(filename);
		if (!src.is_open())
		{
			printf("ERROR UPGRADE: Could not open \"%s\"!\n", filename);
			return false;
		}

		// check if upgrade is possible
		printf("Start upgrading DTB.\n");
		if (tb.UpgradeStart(0x0100) != 0)
		{
			string msg;
			tb.UpgradeErrorMsg(msg);
			printf("ERROR UPGRADE: %s!\n", msg.data());
			return false;
		}

		// download data
		printf("Download running ...\n");
		string rec;
		uint16_t recordCount = 0;
		while (true)
		{
			getline(src, rec);
			if (src.good())
			{
				if (rec.size() == 0) continue;
				recordCount++;
				if (tb.UpgradeData(rec) != 0)
				{
					string msg;
					tb.UpgradeErrorMsg(msg);
					printf("ERROR UPGRADE: %s!\n", msg.data());
					return false;
				}
			}
			else if (src.eof()) break;
			else
			{
				printf("ERROR UPGRADE: Error reading \"%s\"!\n", filename);
				return false;
			}
		}

		if (tb.UpgradeError() != 0)
		{
			string msg;
			tb.UpgradeErrorMsg(msg);
			printf("ERROR UPGRADE: %s!\n", msg.data());
			return false;
		}

		// write EPCS FLASH
		printf("DTB download complete.\n");
		tb.mDelay(200);
		printf("FLASH write start (LED 1..4 on)\n"
			   "DO NOT INTERUPT DTB POWER !\n"
			   "Wait till LEDs goes off.\n"
		       "Restart the DTB.\n");
		tb.UpgradeExec(recordCount);
		tb.Flush();

		return true;
	}

	printf("ERROR UPGRADE: Could not upgrade this DTB version!\n");
	return false;
}


CMD_PROC(upgrade)
{
	char filename[256];
	PAR_STRING(filename, 255);
	UpdateDTB(filename);
	return true;
}

CMD_PROC(rpcinfo)
{
	string name, call, ts;

	tb.GetRpcTimestamp(ts);
	int version = tb.GetRpcVersion();
	int n = tb.GetRpcCallCount();

	printf("--- DTB RPC info ----------------------------------------\n");
	printf("RPC version:     %i.%i\n", version/256, version & 0xff);
	printf("RPC timestamp:   %s\n", ts.c_str());
	printf("Number of calls: %i\n", n);
	printf("Function calls:\n");
	for (int i = 0; i < n; i++)
	{
		tb.GetRpcCallName(i, name);
		rpc_TranslateCallName(name, call);
		printf("%5i: %s\n", i, call.c_str());
	}
	return true;
}

CMD_PROC(info)
{
	string s;
	tb.GetInfo(s);
	printf("--- DTB info ------------------------------------\n%s"
		   "-------------------------------------------------\n", s.c_str());
	return true;
}

CMD_PROC(ver)
{
	string hw;
	tb.GetHWVersion(hw);
	int fw = tb.GetFWVersion();
	int sw = tb.GetSWVersion();
	printf("%s: FW=%i.%02i SW=%i.%02i\n", hw.c_str(), fw/256, fw%256, sw/256, sw%256);
	return true;
}

CMD_PROC(version)
{
	string hw;
	tb.GetHWVersion(hw);
	int fw = tb.GetFWVersion();
	int sw = tb.GetSWVersion();
	printf("%s: FW=%i.%02i SW=%i.%02i\n", hw.c_str(), fw/256, fw%256, sw/256, sw%256);
	return true;
}

CMD_PROC(boardid)
{
	int id = tb.GetBoardId();
	printf("\nBoard Id = %i\n", id);
	return true;
}

CMD_PROC(init)
{
	tb.Init();
	DO_FLUSH
	return true;
}

CMD_PROC(flush)
{
	tb.Flush();
	return true;
}

CMD_PROC(clear)
{
	tb.Clear();
	return true;
}




// =======================================================================
//  delay commands
// =======================================================================

CMD_PROC(udelay)
{
	int del;
	PAR_INT(del, 0, 1000);
	if (del) tb.uDelay(del);
	DO_FLUSH
	return true;
}

CMD_PROC(mdelay)
{
	int ms;
	PAR_INT(ms,1,10000)
	tb.mDelay(ms);
	return true;
}


// =======================================================================
//  test board commands
// =======================================================================


/*
CMD_PROC(clock)
{
	if (tb.isClockPresent())
		printf("clock ok\n");
	else
		printf("clock missing\n");
	return true;
}

CMD_PROC(fsel)
{
	int div;
	PAR_INT(div, 0, 5)

	tb.SetClock(div);
	DO_FLUSH
	return true;
}

CMD_PROC(stretch)
{
	int src, delay, width;
	PAR_INT(src,   0,      3)
	PAR_INT(delay, 0,   1023);
	PAR_INT(width, 0, 0xffff);
	tb.SetClockStretch(src,delay,width);
	DO_FLUSH
	return true;
}
*/

CMD_PROC(clk)
{
	int ns, duty;
	PAR_INT(ns,0,400);
	if (!PAR_IS_INT(duty, -8, 8)) duty = 0;
	tb.Sig_SetDelay(SIG_CLK, ns, duty);
	DO_FLUSH
	return true;
}

CMD_PROC(sda)
{
	int ns, duty;
	PAR_INT(ns,0,400);
	if (!PAR_IS_INT(duty, -8, 8)) duty = 0;
	tb.Sig_SetDelay(SIG_SDA, ns, duty);
	DO_FLUSH
	return true;
}

/*
CMD_PROC(rda)
{
	int ns;
	PAR_INT(ns,0,400);
	tb.SetDelay(DELAYSIG_RDA, ns);
	DO_FLUSH
	return true;
}
*/

CMD_PROC(ctr)
{
	int ns, duty;
	PAR_INT(ns,0,400);
	if (!PAR_IS_INT(duty, -8, 8)) duty = 0;
	tb.Sig_SetDelay(SIG_CTR, ns, duty);
	DO_FLUSH
	return true;
}

CMD_PROC(tin)
{
	int ns, duty;
	PAR_INT(ns,0,400);
	if (!PAR_IS_INT(duty, -8, 8)) duty = 0;
	tb.Sig_SetDelay(SIG_TIN, ns, duty);
	DO_FLUSH
	return true;
}


CMD_PROC(clklvl)
{
	int lvl;
	PAR_INT(lvl,0,15);
	tb.Sig_SetLevel(SIG_CLK, lvl);
	DO_FLUSH
	return true;
}

CMD_PROC(sdalvl)
{
	int lvl;
	PAR_INT(lvl,0,15);
	tb.Sig_SetLevel(SIG_SDA, lvl);
	DO_FLUSH
	return true;
}

CMD_PROC(ctrlvl)
{
	int lvl;
	PAR_INT(lvl,0,15);
	tb.Sig_SetLevel(SIG_CTR, lvl);
	DO_FLUSH
	return true;
}

CMD_PROC(tinlvl)
{
	int lvl;
	PAR_INT(lvl,0,15);
	tb.Sig_SetLevel(SIG_TIN, lvl);
	DO_FLUSH
	return true;
}


CMD_PROC(clkmode)
{
	int mode;
	PAR_INT(mode,0,3);
	if (mode == 3)
	{
		int speed;
		PAR_INT(speed,0,31);
		tb.Sig_SetPRBS(SIG_CLK, speed);
	}
	else
		tb.Sig_SetMode(SIG_CLK, mode);
	DO_FLUSH
	return true;
}

CMD_PROC(sdamode)
{
	int mode;
	PAR_INT(mode,0,3);
	if (mode == 3)
	{
		int speed;
		PAR_INT(speed,0,31);
		tb.Sig_SetPRBS(SIG_SDA, speed);
	}
	else
		tb.Sig_SetMode(SIG_SDA, mode);
	DO_FLUSH
	return true;
}

CMD_PROC(ctrmode)
{
	int mode;
	PAR_INT(mode,0,3);
	if (mode == 3)
	{
		int speed;
		PAR_INT(speed,0,31);
		tb.Sig_SetPRBS(SIG_CTR, speed);
	}
	else
		tb.Sig_SetMode(SIG_CTR, mode);
	DO_FLUSH
	return true;
}

CMD_PROC(tinmode)
{
	int mode;
	PAR_INT(mode,0,3);
	if (mode == 3)
	{
		int speed;
		PAR_INT(speed,0,31);
		tb.Sig_SetPRBS(SIG_TIN, speed);
	}
	else
		tb.Sig_SetMode(SIG_TIN, mode);
	DO_FLUSH
	return true;
}


CMD_PROC(sigoffset)
{
	int offset;
	PAR_INT(offset, 0, 15);
	tb.Sig_SetOffset(offset);
	DO_FLUSH
	return true;
}

CMD_PROC(lvds)
{
	tb.Sig_SetLVDS();
	DO_FLUSH
	return true;
}

CMD_PROC(lcds)
{
	tb.Sig_SetLCDS();
	DO_FLUSH
	return true;
}

/*
CMD_PROC(tout)
{
	int ns;
	PAR_INT(ns,0,450);
	tb.SetDelay(SIGNAL_TOUT, ns);
	DO_FLUSH
	return true;
}

CMD_PROC(trigout)
{
	int ns;
	PAR_INT(ns,0,450);
	tb.SetDelay(SIGNAL_TRGOUT, ns);
	DO_FLUSH
	return true;
}
*/


CMD_PROC(pon)
{
	tb.Pon();
	DO_FLUSH
	return true;
}

CMD_PROC(poff)
{
	tb.Poff();
	DO_FLUSH
	return true;
}

CMD_PROC(va)
{
	int value;
	PAR_INT(value, 0, 4000);
	tb._SetVA(value);
	DO_FLUSH
	return true;
}

CMD_PROC(vd)
{
	int value;
	PAR_INT(value, 0, 4000);
	tb._SetVD(value);
	DO_FLUSH
	return true;
}


CMD_PROC(ia)
{
	int value;
	PAR_INT(value, 0, 1200);
	tb._SetIA(value*10);
	DO_FLUSH
	return true;
}

CMD_PROC(id)
{
	int value;
	PAR_INT(value, 0, 1200);
	tb._SetID(value*10);
	DO_FLUSH
	return true;
}

CMD_PROC(getva)
{
	double v = tb.GetVA();
	printf("\n VA = %1.3fV\n", v);
	return true;
}

CMD_PROC(getvd)
{
	double v = tb.GetVD();
	printf("\n VD = %1.3fV\n", v);
	return true;
}

CMD_PROC(getia)
{
	double i = tb.GetIA();
	printf("\n IA = %1.1fmA\n", i*1000.0);
	return true;
}

CMD_PROC(getid)
{
	double i = tb.GetID();
	printf("\n ID = %1.1fmA\n", i*1000.0);
	return true;
}


CMD_PROC(hvon)
{
	tb.HVon();
	DO_FLUSH
	return true;
}

CMD_PROC(hvoff)
{
	tb.HVoff();
	DO_FLUSH
	return true;
}

CMD_PROC(reson)
{
	tb.ResetOn();
	DO_FLUSH
	return true;
}

CMD_PROC(resoff)
{
	tb.ResetOff();
	DO_FLUSH
	return true;
}


CMD_PROC(status)
{
	uint8_t status = tb.GetStatus();
	printf("SD card detect: %c\n", (status&8) ? '1' : '0');
	printf("CRC error:      %c\n", (status&4) ? '1' : '0');
	printf("Clock good:     %c\n", (status&2) ? '1' : '0');
	printf("CLock present:  %c\n", (status&1) ? '1' : '0');
	return true;
}

CMD_PROC(rocaddr)
{
	int addr;
	PAR_INT(addr, 0, 15)
	tb.SetRocAddress(addr);
	DO_FLUSH
	return true;
}

CMD_PROC(d1)
{
	int sig;
	PAR_INT(sig, 0, 31)
	tb.SignalProbeD1(sig);
	DO_FLUSH
	return true;
}


CMD_PROC(d2)
{
	int sig;
	PAR_INT(sig, 0, 31)
	tb.SignalProbeD2(sig);
	DO_FLUSH
	return true;
}

CMD_PROC(a1)
{
	int sig;
	PAR_INT(sig, 0, 7)
	tb.SignalProbeA1(sig);
	DO_FLUSH
	return true;
}

CMD_PROC(a2)
{
	int sig;
	PAR_INT(sig, 0, 7)
	tb.SignalProbeA2(sig);
	DO_FLUSH
	return true;
}

CMD_PROC(probeadc)
{
	int sig;
	PAR_INT(sig, 0, 7)
	tb.SignalProbeADC(sig);
	DO_FLUSH
	return true;
}

CMD_PROC(pgset)
{
	int addr, delay, bits;
	PAR_INT(addr,0,255)
	PAR_INT(bits,0,63)
	PAR_INT(delay,0,255)
	tb.Pg_SetCmd(addr, (bits<<8) + delay);
	DO_FLUSH
	return true;
}

CMD_PROC(pgstop)
{
	tb.Pg_Stop();
	DO_FLUSH
	return true;
}

CMD_PROC(pgsingle)
{
	tb.Pg_Single();
	DO_FLUSH
	return true;
}

CMD_PROC(pgtrig)
{
	tb.Pg_Trigger();
	DO_FLUSH
	return true;
}

CMD_PROC(pgloop)
{
	int period;
	PAR_INT(period,0,65535)
	tb.Pg_Loop(period);
	DO_FLUSH
	return true;
}


// === DAQ ==================================================================

CMD_PROC(dopen)
{
	int buffersize;
	PAR_INT(buffersize, 0, 60000000);
	buffersize = tb.Daq_Open(buffersize,1);
	printf("%i words allocated for data buffer\n", buffersize);
	if (buffersize == 0) printf("error\n");
	return true;
}

CMD_PROC(dclose)
{
	tb.Daq_Close();
	DO_FLUSH
	return true;
}


CMD_PROC(dstart)
{
	tb.Daq_Start();
	DO_FLUSH
	return true;
}

CMD_PROC(dstop)
{
	tb.Daq_Stop();
	DO_FLUSH
	return true;
}


CMD_PROC(dsize)
{
	int channel;
	PAR_INT(channel,0,2);
	unsigned long size = tb.Daq_GetSize(channel);
	
	printf("size = %u\n", size);
	return true;
}

CMD_PROC(decode)
{
	uint32_t words_remaining = 0;
	vector<uint16_t> data;
        int channel;
        PAR_INT(channel,0,1);
	unsigned long size = tb.Daq_GetSize(channel);
	
        tb.Daq_Read(data,channel, size, words_remaining);



}


CMD_PROC(dread)
{
	uint32_t words_remaining = 0;
	vector<uint16_t> data;
	int channel;
	PAR_INT(channel,0,2);
	tb.Daq_Read(data,channel, 150, words_remaining);
	int size = data.size();
	printf("#samples: %i  remaining: %i\n", size, int(words_remaining));

	for (int i=0; i<size; i++)
	{
		int x = data[i] & 0xffff;
		char s[40];
		HexToBin(x,s);
		printf(" %04X=%s\n", x,s);
		if (i%10 == 9) printf("\n");
	}
	printf("\n");

	for (int i=0; i<size; i++)
	{
		int x = data[i] & 0x0fff;
		Log.printf("%03X", x);
		if (i%100 == 9) printf("\n");
	}
	printf("\n");

	return true;
}

CMD_PROC(dreada)
{
	uint32_t words_remaining = 0;
	vector<uint16_t> data;
	tb.Daq_Read(data, 0,words_remaining);
	int size = data.size();
	printf("#samples: %i remaining: %i\n", size, int(words_remaining));

	for (int i=0; i<size; i++)
	{
		int x = data[i] & 0x0fff;
		if (x & 0x0800) x |= 0xfffff000;
		printf(" %6i", x);
		if (i%10 == 9) printf("\n");
	}
	printf("\n");

	for (int i=0; i<size; i++)
	{
		int x = data[i] & 0x0fff;
		if (x & 0x0800) x |= 0xfffff000;
		Log.printf("%6i", x);
		if (i%100 == 9) printf("\n");
	}
	printf("\n");

	return true;
}

CMD_PROC(takedata)
{
	FILE *f = fopen("daqdata.bin", "wb");
	if (f == 0)
	{
		printf("Could not open data file\n");
		return true;
	}

	uint8_t status = 0;
	uint32_t n;
	vector<uint16_t> data;

	unsigned int sum = 0;
	double mean_n = 0.0;
	double mean_size = 0.0;

	std::cout << hex << (long) &tb << std::endl;

	clock_t t = clock();
	unsigned int memsize = tb.Daq_Open(1000000,1);

	std::cout << memsize << std::endl; 

	tb.Daq_Start();
	clock_t t_start = clock();
	while (!keypressed())
	{
		// read data and status from DTB
		status = tb.Daq_Read(data, 1, 40000, n);

                for(int i = 0 ; i < 1000; i++)
                {
                   char s[40];
                   HexToBin(data[i],s);
                   printf("data[%d]=%s\n",i,s);

                }


	//	printf("%x",data.data());

		// make statistics
		sum += data.size();
		mean_n    = /* 0.2*mean_n    + 0.8* */ n;
		mean_size = /* 0.2*mean_size + 0.8* */ data.size();

		// write statistics every second
//		printf(".");
		if ((clock() - t) > CLOCKS_PER_SEC/4)
		{
			printf("%5.1f%%  %5.0f  %u\n", mean_n*100.0/memsize, mean_size, sum);
			t = clock();
		}

		// write data to file
		if (fwrite(data.data(), sizeof(uint16_t), data.size(), f) != data.size())
		{ printf("\nFile write error"); break; }

		// abort after overflow error
//		if (((status & 1) == 0) && (n == 0)) break;
		if (status & 0x6) break;

//		tb.mDelay(5);
	}
	tb.Daq_Stop();
	double t_run = double(clock() - t_start)/CLOCKS_PER_SEC;

	tb.Daq_Close();

	if (keypressed()) getchar();

	printf("\n");
	if      (status & 4) printf("FIFO overflow\n");
	else if (status & 2) printf("Memory overflow\n");
	printf("Data taking aborted\n%u samples in %0.1f s read (%0.0f samples/s)\n", sum, t_run, sum/t_run);

	fclose(f);
	return true;
}

CMD_PROC(trimroc)
{
	int start,step,thrLevel,nTrig;
	PAR_INT(start,0,255);
	PAR_INT(step,1,20);
	PAR_INT(thrLevel,0,255);
	PAR_INT(nTrig,1,5000);	

	int32_t trim[5000];
	int32_t res[5000];

	//dacReg vcal 0x19, vthrcomp 0x0C 
	int32_t ret = tb.ChipThreshold(start, step, thrLevel, nTrig, 0x0C, 0, 1, res);
	
	printf("Ret=%d\n",ret);

	for(int x = 0 ; x < 4160 ; x++)
	{
		printf("%d : %d \n",x,res[x]);

	}
		
}

#define DECBUFFERSIZE 2048

class Decoder
{
	int printEvery;

	int nReadout;
	int nPixel;

	FILE *f;
	int nSamples;
	uint16_t *samples;

	int x, y, ph;
	void Translate(unsigned long raw);
public:
    Decoder() : printEvery(0), nReadout(0), nPixel(0), f(0), nSamples(0), samples(0) {}
	~Decoder() { Close(); }
	bool Open(const char *filename);
	void Close() { if (f) fclose(f); f = 0; delete[] samples; }
	bool Sample(uint16_t sample);
	void AnalyzeSamples();
	void DumpSamples(int n);
};

bool Decoder::Open(const char *filename)
{
	samples = new uint16_t[DECBUFFERSIZE];
	f = fopen(filename, "wt");
	return f != 0;
}

void Decoder::Translate(unsigned long raw)
{
	ph = (raw & 0x0f) + ((raw >> 1) & 0xf0);
	raw >>= 9;
	int c =    (raw >> 12) & 7;
	c = c*6 + ((raw >>  9) & 7);
	int r =    (raw >>  6) & 7;
	r = r*6 + ((raw >>  3) & 7);
	r = r*6 + ( raw        & 7);
	y = 80 - r/2;
	x = 2*c + (r&1);
}

void Decoder::AnalyzeSamples()
{
	if (nSamples < 1) { nPixel = 0; return; }
	fprintf(f, "%5i: %03X: ", nReadout, (unsigned int)(samples[0] & 0xfff));
	nPixel = (nSamples-1)/2;
	int pos = 1;
	for (int i=0; i<nPixel; i++)
	{
		unsigned long raw = (samples[pos++] & 0xfff) << 12;
		raw += samples[pos++] & 0xfff;
		Translate(raw);
		fprintf(f, " %2i", x);
	}

//	for (pos = 1; pos < nSamples; pos++) fprintf(f, " %03X", int(samples[pos]));
	fprintf(f, "\n");

}

void Decoder::DumpSamples(int n)
{
	if (nSamples < n) n = nSamples;
	for (int i=0; i<n; i++) fprintf(f, " %04X", (unsigned int)(samples[i]));
	fprintf(f, " ... %04X\n", (unsigned int)(samples[nSamples-1]));
}

bool Decoder::Sample(uint16_t sample)
{
	if (sample & 0x8000) // start marker
	{
		if (nReadout && printEvery >= 1000)
		{
			AnalyzeSamples();
			printEvery = 0;
		} else printEvery++;
		nReadout++;
		nSamples = 0;
	}
	if (nSamples < DECBUFFERSIZE)
	{
		samples[nSamples++] = sample;
		return true;
	}
	return false;
}



CMD_PROC(takedata2)
{
	Decoder dec;
	if (!dec.Open("daqdata2.txt"))
	{
		printf("Could not open data file\n");
		return true;
	}

	uint8_t status = 0;
	uint32_t n;
	vector<uint16_t> data;

	unsigned int sum = 0;
	double mean_n = 0.0;
	double mean_size = 0.0;

	clock_t t = clock();
	unsigned long memsize = tb.Daq_Open(1000000,0);
	//tb.Daq_Select_Deser160(deserAdjust);
	tb.Daq_Start();
	clock_t t_start = clock();
	while (!keypressed())
	{
		// read data and status from DTB
		status = tb.Daq_Read(data, 0, 20000, n);

		for(int j = 0; j<200 ; j++)
			printf("%d",data[j]);	
				

		// make statistics
		sum += data.size();
		mean_n    = /* 0.2*mean_n    + 0.8* */ n;
		mean_size = /* 0.2*mean_size + 0.8* */ data.size();

		// write statistics every second
//		printf(".");
		if ((clock() - t) > CLOCKS_PER_SEC)
		{
			printf("%5.1f%%  %5.0f  %u\n", mean_n*100.0/memsize, mean_size, sum);
			t = clock();
		}

		// decode file
		for (unsigned int i=0; i<data.size(); i++) dec.Sample(data[i]);

		// abort after overflow error
		if (((status & 1) == 0) && (n == 0)) break;

		tb.mDelay(5);
	}
	tb.Daq_Stop();
	double t_run = double(clock() - t_start)/CLOCKS_PER_SEC;

	tb.Daq_Close();

	if (keypressed()) getchar();

	printf("\nstatus: %02X ", int(status));
	if      (status & 4) printf("FIFO overflow\n");
	else if (status & 2) printf("Memory overflow\n");
	printf("Data taking aborted\n%u samples in %0.1f s read (%0.0f samples/s)\n", sum, t_run, sum/t_run);

	return true;
}

CMD_PROC(showclk)
{
	const unsigned int nSamples = 20;
	const int gain = 1;
//	PAR_INT(gain,1,4);

	unsigned int i, k;
	vector<uint16_t> data[20];

	tb.Pg_Stop();
	tb.Pg_SetCmd( 0, PG_SYNC +  5);
	tb.Pg_SetCmd( 1, PG_CAL  +  6);
	tb.Pg_SetCmd( 2, PG_TRG  +  6);
	tb.Pg_SetCmd( 3, PG_RESR +  6);
	tb.Pg_SetCmd( 4, PG_REST +  6);
	tb.Pg_SetCmd( 5, PG_TOK);

	tb.SignalProbeD1(9);
	tb.SignalProbeD2(17);
	tb.SignalProbeA2(PROBEA_CLK);
	tb.uDelay(10);
	tb.SignalProbeADC(PROBEA_CLK, gain-1);
	tb.uDelay(10);

	tb.Daq_Select_ADC(nSamples, 1, 1);
	tb.uDelay(1000);
	tb.Daq_Open(1024,0);
	for (i=0; i<20; i++)
	{
		tb.Sig_SetDelay(SIG_CLK, 26-i);
		tb.uDelay(10);
		tb.Daq_Start();
		tb.Pg_Single();
		tb.uDelay(1000);
		tb.Daq_Stop();
		printf("Size: %d\n",tb.Daq_GetSize());
		
		tb.Daq_Read(data[i],0, 1024);
		if (data[i].size() != nSamples)
		{
			printf("Data size %i: %i\n", i, int(data[i].size()));
			return true;
		}
	}
	tb.Daq_Close();
	tb.Flush();

	int n = 20*nSamples;
	vector<double> values(n);
	int x = 0;
	for (k=0; k<nSamples; k++) for (i=0; i<20; i++)
	{
		printf("%d=%d",i,data[i][k]);
		int y = (data[i])[k] & 0x0fff;
		if (y & 0x0800) y |= 0xfffff000;
		values[x++] = y;
		printf(" = %d\n",y);
	}
	Scope("CLK", values);

	return true;
}

CMD_PROC(showctr)
{
	const unsigned int nSamples = 60;
	const int gain = 1;
//	PAR_INT(gain,1,4);

	unsigned int i, k;
	vector<uint16_t> data[20];

	tb.Pg_Stop();
	tb.Pg_SetCmd( 0, PG_SYNC +  5);
	tb.Pg_SetCmd( 1, PG_CAL  +  6);
	tb.Pg_SetCmd( 2, PG_TRG  +  6);
	tb.Pg_SetCmd( 3, PG_RESR +  6);
	tb.Pg_SetCmd( 4, PG_REST +  6);
	tb.Pg_SetCmd( 5, PG_TOK);

	tb.SignalProbeD1(9);
	tb.SignalProbeD2(17);
	tb.SignalProbeA2(PROBEA_CTR);
	tb.uDelay(10);
	tb.SignalProbeADC(PROBEA_CTR, gain-1);
	tb.uDelay(10);

	tb.Daq_Select_ADC(nSamples, 1, 1);
	tb.uDelay(1000);
	tb.Daq_Open(1024,0);
	for (i=0; i<20; i++)
	{
		tb.Sig_SetDelay(SIG_CTR, 26-i);
		tb.uDelay(10);
		tb.Daq_Start();
		tb.Pg_Single();
		tb.uDelay(1000);
		tb.Daq_Stop();
		tb.Daq_Read(data[i], 0, 1024);
		if (data[i].size() != nSamples)
		{
			printf("Data size %i: %i\n", i, int(data[i].size()));
			return true;
		}
	}
	tb.Daq_Close();
	tb.Flush();

	int n = 20*nSamples;
	vector<double> values(n);
	int x = 0;
	for (k=0; k<nSamples; k++) for (i=0; i<20; i++)
	{
		int y = (data[i])[k] & 0x0fff;
		if (y & 0x0800) y |= 0xfffff000;
		values[x++] = y;
	}
	Scope("CTR", values);

/*
	FILE *f = fopen("X:\\developments\\adc\\data\\adc.txt", "wt");
	if (!f) { printf("Could not open File!\n"); return true; }
	double t = 0.0;
	for (k=0; k<100; k++) for (i=0; i<20; i++)
	{
		int x = (data[i])[k] & 0x0fff;
		if (x & 0x0800) x |= 0xfffff000;
		fprintf(f, "%7.2f %6i\n", t, x);
		t += 1.25;
	}
	fclose(f);
*/
	return true;
}


CMD_PROC(showsda)
{
	const unsigned int nSamples = 52;
	unsigned int i, k;
	vector<uint16_t> data[20];

	tb.SignalProbeD1(9);
	tb.SignalProbeD2(17);
	tb.SignalProbeA2(PROBEA_SDA);
	tb.uDelay(10);
	tb.SignalProbeADC(PROBEA_SDA, 0);
	tb.uDelay(10);

	tb.Daq_Select_ADC(nSamples, 2, 7);
	tb.uDelay(1000);
	tb.Daq_Open(1024,0);
	for (i=0; i<20; i++)
	{
		tb.Sig_SetDelay(SIG_SDA, 26-i);
		tb.uDelay(10);
		tb.Daq_Start();
		tb.roc_Pix_Trim(12, 34, 5);
		tb.uDelay(1000);
		tb.Daq_Stop();
		tb.Daq_Read(data[i], 0, 1024);
		if (data[i].size() != nSamples)
		{
			printf("Data size %i: %i\n", i, int(data[i].size()));
			return true;
		}
	}
	tb.Daq_Close();
	tb.Flush();

	int n = 20*nSamples;
	vector<double> values(n);
	int x = 0;
	for (k=0; k<nSamples; k++) for (i=0; i<20; i++)
	{
		int y = (data[i])[k] & 0x0fff;
		if (y & 0x0800) y |= 0xfffff000;
		values[x++] = y;
	}
	Scope("SDA", values);

	return true;
}




CMD_PROC(adcena)
{
	int datasize;
	PAR_INT(datasize, 1, 2047);
	tb.Daq_Select_ADC(datasize, 1, 4, 6);
	DO_FLUSH
	return true;
}

CMD_PROC(adcdis)
{
	tb.Daq_Select_Deser160(2);
	DO_FLUSH
	return true;
}

CMD_PROC(deser)
{
	int shift;
	PAR_INT(shift,0,7);
	tb.Daq_Select_Deser160(shift);
	DO_FLUSH
	return true;
}



void decoding_show2(vector<uint16_t> &data)
{
	int size = data.size();
	if (size > 6) size = 6;
	for (int i=0; i<size; i++)
	{
		uint16_t x = data[i];
		for (int k=0; k<12; k++)
		{
			Log.puts((x & 0x0800)? "1" : "0");
			x <<= 1;
			if (k == 3 || k == 7 || k == 11) Log.puts(" ");
		}
	}
}

void decoding_show(vector<uint16_t> *data)
{
	int i;
	for (i=1; i<16; i++) if (data[i] != data[0]) break;
	decoding_show2(data[0]);
	Log.puts("  ");
	if (i<16) decoding_show2(data[i]); else Log.puts(" no diff");
	Log.puts("\n");
}

CMD_PROC(decoding)
{
	unsigned short t;
	vector<uint16_t> data[16];
	tb.Pg_SetCmd(0, PG_TOK + 0);
	tb.Daq_Select_Deser160(2);
	tb.uDelay(10);
	Log.section("decoding");
	tb.Daq_Open(100,0);
	for (t=0; t<44; t++)
	{
		tb.Sig_SetDelay(SIG_CLK, t);
		tb.Sig_SetDelay(SIG_TIN, t+5);
		tb.uDelay(10);
		for (int i=0; i<16; i++)
		{
			tb.Daq_Start();
			tb.Pg_Single();
			tb.uDelay(200);
			tb.Daq_Stop();
			tb.Daq_Read(data[i], 0, 200);
		}
		Log.printf("%3i ", int(t));
		decoding_show(data);
	}
	tb.Daq_Close();
	Log.flush();
	return true;
}



// -- Wafer Test Adapter commands ----------------------------------------
/*
CMD_PROC(vdreg)    // regulated VD
{
	double v = tb.GetVD_Reg();
	printf("\n VD_reg = %1.3fV\n", v);
	return true;
}

CMD_PROC(vdcap)    // unregulated VD for contact test
{
	double v = tb.GetVD_CAP();
	printf("\n VD_cap = %1.3fV\n", v);
	return true;
}

CMD_PROC(vdac)     // regulated VDAC
{
	double v = tb.GetVDAC_CAP();
	printf("\n V_dac = %1.3fV\n", v);
	return true;
}
*/


// =======================================================================
//  tbm commands
// =======================================================================
CMD_PROC(tbmdis)
{
	tb.tbm_Enable(false);
	DO_FLUSH
	return true;
}

CMD_PROC(tbmsel)
{
	int hub, port;
	PAR_INT(hub,0,31);
	PAR_INT(port,0,7);
	tb.tbm_Enable(true);
	tb.tbm_Addr(hub,port);
	DO_FLUSH
	return true;
}

CMD_PROC(modsel)
{
	int hub;
	PAR_INT(hub,0,31);
	tb.tbm_Enable(true);
	tb.mod_Addr(hub);
	DO_FLUSH
	return true;
}

CMD_PROC(tbmset)
{
	int reg, value;
	PAR_INT(reg,0,255);
	PAR_INT(value,0,255);
	tb.tbm_Set(reg,value);

	DO_FLUSH
	return true;
}

CMD_PROC(tbmget)
{
	int reg;
	unsigned char value;
	PAR_INT(reg,0,255);
	if (tb.tbm_Get(reg,value))
	{
		printf(" reg 0x%02X = %3i (0x%02X)\n", reg, (int)value, (int)value);
	} else puts(" error\n");

	return true;
}

CMD_PROC(tbmgetraw)
{
	int reg;
	uint32_t value;
	PAR_INT(reg,0,255);
	if (tb.tbm_GetRaw(reg,value))
	{
		printf("value=0x%02X (Hub=%2i; Port=%i; Reg=0x%02X; inv=0x%X; stop=%c)\n",
			value & 0xff, (value>>19)&0x1f, (value>>16)&0x07,
			(value>>8)&0xff, (value>>25)&0x1f, (value&0x1000)?'1':'0');
	} else puts("error\n");

	return true;
}


void PrintTbmData(int reg, int value)
{
	if (value < 0)
	{
		printf("02X: ?|        |\n", reg);
		return;
	}
	printf("%02X:%02X", reg, value);
	switch (reg & 0x0e)
	{
	case 0:
		printf("|        |");
		if (value&0x40) printf("TrigOut |");   else printf("        |");
		if (value&0x20) printf("Stack rd|");   else printf("        |");
		if (value&0x20) printf("        |");   else printf("Trig In |");
		if (value&0x10) printf("Pause RO|");   else printf("        |");
		if (value&0x08) printf("Stack Fl|");   else printf("        |");
		if (value&0x02) printf("        |");   else printf("TBM Clk |");
		if (value&0x01) printf(" Full RO|\n"); else printf("Half RO |\n");
		break;

	case 2:
		if (value&0x80)                printf("| Mode:Sync       |");
		else if ((value&0xc0) == 0x80) printf("| Mode:Clear EvCtr|");
		else                           printf("| Mode:Pre-Cal    |");

		printf(" StackCount: %2i                                      |\n",
			value&0x3F);
		break;

	case 4:
		printf("| Event Number: %3i             "
			"                                        |\n", value&0xFF);
		break;

	case 6:
		if (value&0x80) printf("|        |");   else printf("|Tok Pass|");
		if (value&0x40) printf("TBM Res |");   else printf("        |");
		if (value&0x20) printf("ROC Res |");   else printf("        |");
		if (value&0x10) printf("Sync Err|");   else printf("        |");
		if (value&0x08) printf("Sync    |");   else printf("        |");
		if (value&0x04) printf("Clr TCnt|");   else printf("        |");
		if (value&0x02) printf("PreCalTr|");   else printf("        |");
		if (value&0x01) printf("Stack Fl|\n"); else printf("        |\n");
		break;

	case 8:
		printf("|        |        |        |        |        |");
		if (value&0x04) printf("Force RO|");   else printf("        |");
		if (value&0x02) printf("        |");   else printf("Tok Driv|");
		if (value&0x01) printf("        |\n"); else printf("Ana Driv|\n");
		break;
	}
}

const char regline[] =
"+--------+--------+--------+--------+--------+--------+--------+--------+";

CMD_PROC(tbmregs)
{
	const int reg[13] =
	{
		0xE1,0xE3,0xE5,0xE7,0xE9,
		0xEB,0xED,0xEF,
		0xF1,0xF3,0xF5,0xF7,0xF9
	};
	int i;
	int value[13];
	unsigned char data;
	int ret = 0;

	for (i=0; i<13; i++)
		if (ret = tb.tbm_Get(reg[i],data)) value[i] = data; else value[i] = -1;
	printf(" return: %d\n",ret);
	printf(" reg  TBMA   TBMB\n");
	printf("TBMA %s\n", regline);
	for (i=0; i<5;  i++) PrintTbmData(reg[i],value[i]);
	printf("TBMB %s\n", regline);
	for (i=8; i<13; i++) PrintTbmData(reg[i],value[i]);
	printf("     %s\n", regline);
	for (i=5; i<8; i++)
	{
		printf("%02X:", reg[i]);
		if (value[i]>=0) printf("%02X\n", value[i]);
		else printf(" ?\n");

	}
	return true;
}

CMD_PROC(modscan)
{
	int hub;
	unsigned char data;
	printf(" hub:");
	for (hub=0; hub<32; hub++)
	{
		tb.mod_Addr(hub);
		if (tb.tbm_Get(0xe1,data)) printf(" %2i", hub);
	}
	puts("\n");
	return true;
}

// =======================================================================
//  roc commands
// =======================================================================
int roclist[16] = {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};


CMD_PROC(select)
{
	int rocmin, rocmax, i;
	PAR_RANGE(rocmin,rocmax, 0,15)

	for (i=0; i<rocmin;  i++) roclist[i] = 0;
	for (;    i<=rocmax; i++) roclist[i] = 1;
	for (;    i<16;      i++) roclist[i] = 0;

	cout << "Init ROC: " << rocmin << endl;
	tb.roc_I2cAddr(rocmin);
	cout << "Init successful" << endl;
	return true;
}

CMD_PROC(dac)
{
	int addr, value;
	PAR_INT(addr,1,255)
	PAR_INT(value,0,255)

	for (int i=0; i<16; i++)
		 if (roclist[i])
		 {
		   std::cout << "Setting dac " << addr << " for ROC " << i << endl;
		    tb.roc_I2cAddr(i);
		    tb.roc_SetDAC(addr, value);
		   std::cout << "Set dac "<< addr << " for ROC " << i << endl;
		 }

	DO_FLUSH
	return true;
}

CMD_PROC(vana)
{
	int value;
	PAR_INT(value,0,255)
	for (int i=0; i<16; i++) if (roclist[i])
	{ tb.roc_I2cAddr(i); tb.roc_SetDAC(Vana, value); }

	DO_FLUSH
	return true;
}

CMD_PROC(vtrim)
{
	int value;
	PAR_INT(value,0,255)
	for (int i=0; i<16; i++) if (roclist[i])
	{ tb.roc_I2cAddr(i); tb.roc_SetDAC(Vtrim, value); }

	DO_FLUSH
	return true;
}

CMD_PROC(vthr)
{
	int value;
	PAR_INT(value,0,255)
	for (int i=0; i<16; i++) if (roclist[i])
	{ tb.roc_I2cAddr(i); tb.roc_SetDAC(VthrComp, value); }

	DO_FLUSH
	return true;
}

CMD_PROC(vcal)
{
	int value;
	PAR_INT(value,0,255)
	for (int i=0; i<16; i++) if (roclist[i])
	{ tb.roc_I2cAddr(i); tb.roc_SetDAC(Vcal, value); }

	DO_FLUSH
	return true;
}

CMD_PROC(wbc)
{
	int value;
	PAR_INT(value,0,255)
	for (int i=0; i<16; i++) if (roclist[i])
	{ tb.roc_I2cAddr(i);  tb.roc_SetDAC(WBC, value); }

	DO_FLUSH
	return true;
}

CMD_PROC(ctl)
{
	int value;
	PAR_INT(value,0,255)
	for (int i=0; i<16; i++) if (roclist[i])
	{ tb.roc_I2cAddr(i); tb.roc_SetDAC(CtrlReg, value); }

	DO_FLUSH
	return true;
}

CMD_PROC(cole)
{
	int col, colmin, colmax;
	PAR_RANGE(colmin,colmax, 0,ROC_NUMCOLS-1)
	for (int i=0; i<16; i++) if (roclist[i])
	{
		tb.roc_I2cAddr(i);
		for (col=colmin; col<=colmax; col+=2)
			tb.roc_Col_Enable(col, 1);
	}

	DO_FLUSH
	return true;
}

CMD_PROC(cold)
{
	int col, colmin, colmax;
	PAR_RANGE(colmin,colmax, 0,ROC_NUMCOLS-1)
	for (int i=0; i<16; i++) if (roclist[i])
	{
		tb.roc_I2cAddr(i);
		for (col=colmin; col<=colmax; col+=2)
			tb.roc_Col_Enable(col, 0);
	}

	DO_FLUSH
	return true;
}

CMD_PROC(pixe)
{
	int col,colmin,colmax, row,rowmin,rowmax, value;
	PAR_RANGE(colmin,colmax, 0,ROC_NUMCOLS-1)
	PAR_RANGE(rowmin,rowmax, 0,ROC_NUMROWS-1)
	PAR_INT(value,0,15)
	for (int i=0; i<16; i++) if (roclist[i])
	{
		tb.roc_I2cAddr(i);
		for(col=colmin; col<=colmax; col++) for (row=rowmin; row<=rowmax; row++)
			tb.roc_Pix_Trim(col,row, 15-value);
	}

	DO_FLUSH
	return true;
}

CMD_PROC(pixd)
{
	int col,colmin,colmax, row,rowmin,rowmax;
	PAR_RANGE(colmin,colmax, 0,ROC_NUMCOLS-1)
	PAR_RANGE(rowmin,rowmax, 0,ROC_NUMROWS-1)
	for (int i=0; i<16; i++) if (roclist[i])
	{
		tb.roc_I2cAddr(i);
		for(col=colmin; col<=colmax; col++) for (row=rowmin; row<=rowmax; row++)
			tb.roc_Pix_Mask(col,row);
	}

	DO_FLUSH
	return true;
}

CMD_PROC(cal)
{
	int col,colmin,colmax, row,rowmin,rowmax;
	PAR_RANGE(colmin,colmax, 0,ROC_NUMCOLS-1)
	PAR_RANGE(rowmin,rowmax, 0,ROC_NUMROWS-1)
	for (int i=0; i<16; i++) if (roclist[i])
	{
		tb.roc_I2cAddr(i);
		for(col=colmin; col<=colmax; col++) for (row=rowmin; row<=rowmax; row++)
			tb.roc_Pix_Cal(col,row,false);
	}

	DO_FLUSH
	return true;
}

CMD_PROC(cals)
{
	int col,colmin,colmax, row,rowmin,rowmax;
	PAR_RANGE(colmin,colmax, 0,ROC_NUMCOLS-1)
	PAR_RANGE(rowmin,rowmax, 0,ROC_NUMROWS-1)
	for (int i=0; i<16; i++) if (roclist[i])
	{
		tb.roc_I2cAddr(i);
		for(col=colmin; col<=colmax; col++) for (row=rowmin; row<=rowmax; row++)
			tb.roc_Pix_Cal(col,row,true);
	}

	DO_FLUSH
	return true;
}

CMD_PROC(cald)
{
	for (int i=0; i<16; i++) if (roclist[i])
	{
		tb.roc_I2cAddr(i);
		tb.roc_ClrCal();
	}

	DO_FLUSH
	return true;
}

CMD_PROC(mask)
{
	for (int i=0; i<16; i++) if (roclist[i])
	{
		tb.roc_I2cAddr(i);
		tb.roc_Chip_Mask();
	}

	DO_FLUSH
	return true;
}




// =======================================================================
//  experimential ROC test commands
// =======================================================================

/*CMD_PROC(ethsend)
{
	char msg[45];
	PAR_STRINGEOL(msg,45);
	string message = msg;
	tb.Ethernet_Send(message);
	DO_FLUSH
	return true;
}

CMD_PROC(ethrx)
{
	unsigned int n = tb.Ethernet_RecvPackets();
	printf("%u packets received\n", n);
	return true;
}*/


void PrintScale(int min, int max)
{
	int x;
	Log.puts("     |");
	for (x=min; x<=max; x++) Log.printf("%i", (x/100)%10);
	Log.puts("\n     |");
	for (x=min; x<=max; x++) Log.printf("%i", (x/10)%10);
	Log.puts("\n     |");
	for (x=min; x<=max; x++) Log.printf("%i", x%10);
	Log.puts("\n-----+");
	for (x=min; x<=max; x++) Log.puts("-");
	Log.puts("\n");
}

void Scan1D(int vx, int xmin, int xmax)
{
	int x, k;
	vector<uint16_t> data;

	// --- take data
	tb.Daq_Start();
	for (x = xmin; x<=xmax; x++)
	{
		tb.roc_SetDAC(vx, x);
		tb.uDelay(100);
		for (k=0; k<10; k++)
		{
			tb.Pg_Single();
			tb.uDelay(5);
		}
	}
	tb.Daq_Stop();
	tb.Daq_Read(data, 0, 10000);

	// --- analyze data
	int pos = 0, count;
	int spos = 0;
	char s[260];
	PixelReadoutData pix;

	try
	{
		for (x = xmin; x<=xmax; x++)
		{
			count = 0;
			for (k=0; k<10; k++)
			{
				DecodePixel(data, pos, pix);
				if (pix.n > 0) count++;
			}
			if (count == 0) s[spos++] = '.';
			else if (count >= 10) s[spos++] = '*';
			else s[spos++] = count + '0';
		}
	} catch (int) {}
	s[spos] = 0;
	Log.printf("%s\n", s);
	Log.flush();
}


CMD_PROC(shmoo)
{
	int vx, xmin, xmax, vy, ymin, ymax;
	PAR_INT(vx, 0, 0xff);
	PAR_RANGE(xmin,xmax, 0,255);
	PAR_INT(vy, 0, 0xff);
	PAR_RANGE(ymin,ymax, 0,255);

	int count = xmax-xmin;
	if (count < 1 || count > 256) return true;

	Log.section("SHMOO",false);
	Log.printf("regX(%i)=%i:%i;  regY(%i)=%i:%i\n",
		vx, xmin, xmax, vy, ymin, ymax);

	tb.Daq_Open(50000,0);
	tb.Daq_Select_Deser160(deserAdjust);

	PrintScale(xmin, xmax);
	for (int y=ymin; y<=ymax; y++)
	{
		tb.roc_SetDAC(vy,y);
		tb.uDelay(100);
		Log.printf("%5i|", y);
		Scan1D(vx, xmin, xmax);
	}

	tb.Daq_Close();

	return true;
}




CMD_PROC(phscan)
{
	int col, row;
	PAR_INT(col, 0, 51)
	PAR_INT(row, 0, 79)

	const int vcalmin = 0;
	const int vcalmax = 140;

	// load settings
//	tb.roc_SetDAC(CtrlReg,0x00); // 0x04

	tb.Pg_Stop();
	tb.Pg_SetCmd(0, PG_RESR + 15);
	tb.Pg_SetCmd(1, PG_CAL  + 20);
	tb.Pg_SetCmd(2, PG_TRG  + 16);
	tb.Pg_SetCmd(3, PG_TOK);

	tb.roc_SetDAC(WBC,  15);

	tb.Daq_Open(50000,0);
	tb.Daq_Select_Deser160(deserAdjust);
	tb.Daq_Start();

	// --- scan vcal
	tb.roc_Col_Enable(col, true);
	tb.roc_Pix_Trim(col, row, 15);
	tb.roc_Pix_Cal (col, row, false);

	for (int cal = vcalmin; cal < vcalmax; cal++)
	{
		tb.roc_SetDAC(Vcal, cal);
		tb.uDelay(100);
		for (int k=0; k<5; k++)
		{
			tb.Pg_Single();
			tb.uDelay(50);
		}
	}

	tb.roc_Pix_Mask(col, row);
	tb.roc_Col_Enable(col, false);
	tb.roc_ClrCal();

	tb.Daq_Stop();
	vector<uint16_t> data;
	tb.Daq_Read(data, 0, 4000);
	tb.Daq_Close();

	// --- plot data
	PixelReadoutData pix;
	int pos = 0;
	int dpos = 0;
	vector<double> y(vcalmax-vcalmin, 0.0);
	try
	{
		for (int cal = vcalmin; cal < vcalmax; cal++)
		{
			int cnt = 0;
			double yi = 0.0;
			for (int k=0; k<5; k++)
			{
				DecodePixel(data, pos, pix);
				if (pix.n > 0) { yi += pix.p; cnt++; }
			}
			y[dpos++] = (cnt > 0) ? yi/cnt : 0.0;
		}
	} catch (int e)
	{
		printf("Read error %i\n", e);
		if (data.size() > 5) for (int i=0; i<=5; i++) printf(" %04X", int(data[i]));
		printf("\n");
		return true;
	}

	PlotData("ADC scan", "Vcal", "ADC value", double(vcalmin), double(vcalmax), y);

	return true;
}


CMD_PROC(deser160)
{
	tb.Daq_Open(1000,0);
	tb.Pg_SetCmd(0, PG_TOK);

	vector<uint16_t> data;

	vector<std::pair<int,int> > goodvalues;

	int x, y;
	printf("      0     1     2     3     4     5     6     7\n");
	for (y = 0; y < 20; y++)
	{
		printf("%2i:", y);
		for (x = 0; x < 8; x++)
		{
			tb.Daq_Select_Deser160(x);
			tb.Sig_SetDelay(SIG_CLK,  y);
			tb.Sig_SetDelay(SIG_SDA,  y+15);
			tb.Sig_SetDelay(SIG_CTR,  y);
			tb.Sig_SetDelay(SIG_TIN,  y+5);
			tb.uDelay(10);

			tb.Daq_Start();
			tb.Pg_Single();
			tb.uDelay(10);
			tb.Daq_Stop();
			tb.Daq_Read(data, 0, 100);

			if (data.size())
			{
				int h = data[0] & 0xffc;
				if (h == 0x7f8)
				{
					printf(" <%03X>", int(data[0] & 0xffc));
					goodvalues.push_back(std::make_pair(y,x));
				}
				else
					printf("  %03X ", int(data[0] & 0xffc));
			}
			else printf("  ... ");
		}
		printf("\n");
	}
	tb.Daq_Close();
	printf("Old values: %i %i\n", delayAdjust, deserAdjust);
	if (goodvalues.size() == 0)
	{
	    printf("No value found where header could be read back - no adjustments made.\n");
	    return true;
	}
	printf("Good values are:\n");
	for (std::vector<std::pair<int,int> >::const_iterator it = goodvalues.begin(); it != goodvalues.end(); it++)
	{
	    printf("%i %i\n", it->first, it->second);
	}
	const int select = floor( 0.5*goodvalues.size() - .5);
	delayAdjust = goodvalues[select].first;
	deserAdjust = goodvalues[select].second;
	printf("New values: %i %i\n", delayAdjust, deserAdjust);

	return true;
}


/*
0000 I2C data
0001 I2C address
0010 I2C pixel column
0011 I2C pixel row

1000 VD unreg
1001 VA unreg
1010 VA reg
1100 IA

{ rocaddr[3:0], sana, s[2:0], data[7:0] }

*/

CMD_PROC(readback)
{
	int i;

	tb.Daq_Open(100,0);
	tb.Pg_Stop();
	tb.Pg_SetCmd(0, PG_TOK);
	tb.Daq_Select_Deser160(deserAdjust);

	// --- take data
	tb.Daq_Start();
	for (i=0; i<36; i++)
	{
		tb.Pg_Single();
		tb.uDelay(20);
	}
	tb.Daq_Stop();

	// read out data
	vector<uint16_t> data;
	tb.Daq_Read(data, 0, 40);
	tb.Daq_Close();

	//analyze data
	int pos = 0;
	PixelReadoutData pix;

	unsigned int value = 0;
	try
	{
		// find start bit
		do
		{
			DecodePixel(data, pos, pix);
		} while ((pix.hdr & 2) == 0);

		// read data
		for (i=0; i<16; i++)
		{
			DecodePixel(data, pos, pix);
			value = (value << 1) + (pix.hdr & 1);
		}

	} catch (int e)
	{
		printf("error %i (size=%i, pos=%i)\n", e, int(data.size()), pos);
		return true;
	}

	int roc = (value >> 12) & 0xf;
	int cmd = (value >>  8) & 0xf;
	int x = value & 0xff;

	printf("%04X: roc[%i].", value, roc);
	switch (cmd)
	{
	case  0: printf("I2C_data = %02X\n", x); break;
	case  1: printf("I2C_addr = %02X\n", x); break;
	case  2: printf("col = %02X\n", x); break;
	case  3: printf("row = %02X\n", x); break;
	case  8: printf("VD_unreg = %02X\n", x); break;
	case  9: printf("VA_unreg = %02X\n", x); break;
	case 10: printf("VA_reg = %02X\n", x); break;
	case 12: printf("IA = %02X\n", x); break;
	default: printf("? = %04X\n", value); break;
	}

	return true;
}



// =======================================================================
//  chip/wafer test commands
// =======================================================================


int chipPos = 0;

char chipPosChar[] = "ABCD";


void GetTimeStamp(char datetime[])
{
	time_t t;
	struct tm *dt;
	time(&t);
	dt = localtime(&t);
	strcpy(datetime, asctime(dt));
}


bool ReportWafer()
{
	char *msg;
	Log.section("WAFER", false);

	// ProductID
	msg = prober.printf("GetProductID");
	if (strlen(msg)<=3)
	{
		printf("missing wafer product id!\n");
		Log.printf("productId?\n");
		return false;
	}
	Log.printf("%s", msg+3);
	strcpy(g_chipdata.productId, msg+3);

	// WaferID
	msg = prober.printf("GetWaferID");
	if (strlen(msg)<=3)
	{
		printf(" missing wafer id!\n");
		Log.printf(" waferId?\n");
		return false;
	}
	Log.printf(" %s", msg+3);
	strcpy(g_chipdata.waferId, msg+3);

	// Wafer Number
	int num;
	msg = prober.printf("GetWaferNum");
	if (strlen(msg)>3) if (sscanf(msg+3, "%i", &num) == 1)
	{
		Log.printf(" %i\n", num);
		strcpy(g_chipdata.waferNr, msg+3);
		return true;
	}

	printf(" missing wafer number!\n");
	Log.printf(" wafernum?\n");
	return false;
}


bool ReportChip(int &x, int &y)
{
	char *pos = prober.printf("ReadMapPosition");
	int len = strlen(pos);
	if (len<3) return false;
	pos += 3;

	float posx, posy;
	if (sscanf(pos, "%i %i %f %f", &x, &y, &posx, &posy) != 4)
	{
		printf(" error reading chip information\n");
		return false;
	}
	nEntry++;
	printf("#%05i: %i%i%c -> ", nEntry, y, x, chipPosChar[chipPos]);
	fflush(stdout);
	Log.section("CHIP", false);
	Log.printf(" %i %i %c %9.1f %9.1f\n",
		x, y, chipPosChar[chipPos], posx, posy);
	g_chipdata.mapX   = x;
	g_chipdata.mapY   = y;
	g_chipdata.mapPos = chipPos;
	return true;
}


CMD_PROC(pr)
{
	char s[256];
	PAR_STRINGEOL(s,250);

	printf(" REQ %s\n", s);
	char *answer = prober.printf("%s", s);
	printf(" RSP %s\n", answer);
	return true;
}


CMD_PROC(sep)
{
	prober.printf("MoveChuckSeparation");
	return true;
}


CMD_PROC(contact)
{
	prober.printf("MoveChuckContact");
	return true;
}


bool test_wafer()
{
	int x, y;

	g_chipdata.Invalidate();

	if (!ReportWafer()) return true;
	if (!ReportChip(x,y)) return true;
	g_chipdata.nEntry = nEntry;

	GetTimeStamp(g_chipdata.startTime);
	Log.timestamp("BEGIN");
	tb.SetLed(0x10);
	bool repeat;
	int bin = test_roc_dig(repeat);
	tb.SetLed(0x00);
	tb.Flush();
	GetTimeStamp(g_chipdata.endTime);
	Log.timestamp("END");
	Log.puts("\n");
	Log.flush();
	printf("%3i\n", bin);

	printf(" RSP %s\n", prober.printf("BinMapDie %i", bin));

	return true;
}


bool test_chip(char chipid[])
{
	nEntry++;

	g_chipdata.Invalidate();
	g_chipdata.nEntry = nEntry;
	printf("#%05i: %s -> ", nEntry, chipid);
	fflush(stdout);
	Log.section("CHIP1", false);
	Log.printf(" %s\n", chipid);
	strcpy(g_chipdata.chipId, chipid);

	GetTimeStamp(g_chipdata.startTime);
	Log.timestamp("BEGIN");

	tb.SetLed(0x10);
	bool repeat;
	int bin = test_roc_dig(repeat);
	tb.SetLed(0x00);
	tb.Flush();

	GetTimeStamp(g_chipdata.endTime);
	Log.timestamp("END");
	Log.puts("\n");
	Log.flush();

	printf("%3i\n", bin);

	return true;
}

CMD_PROC(modalive)
{


 vector<uint16_t> data;
/*
   FILE *fp;
   char str[20];

   // opening file for reading 
   fp = fopen("modalivefull.txt" , "r");
   if(fp == NULL) {
      perror("Error opening file");
   }

   while( fgets (str, 20 , fp)!=NULL ) {
      int n;
      stringstream(str) >> std::hex >> n;
      data.push_back(n);
      char s[20];
	HexToBin(n,s);
	cout << s << endl;
   }
   fclose(fp);

 cout << "Loaded " << data.size() << endl; 
 
*/
 //data.push_back( 0xBFE0 ); //=1011111111100000
 //data.push_back( 0x080B ); //=0000100000001011
 //data.push_back( 0xFC3F ); //=1111110000111111
 //data.push_back( 0xC3FC ); //=1100001111111100
 //data.push_back( 0x3FC3 ); //=0011111111000011
 //data.push_back( 0xFC3F ); //=1111110000111111
 //data.push_back( 0xC3FC ); //=1100001111111100
 //data.push_back( 0x3FC3 ); //=0011111111000011
 //data.push_back( 0xFFB1 ); //=1111111110110001
 //data.push_back( 0x007F ); //=0000000001111111

 int npx[16] = {0};
 int modph[16][52][80] = {{{0}}};

 for( size_t iroc = 0; iroc < 16; ++iroc ) {

 //  if( roclist[iroc] == 0 ) continue;

   tb.roc_I2cAddr(iroc);
   cout << endl << setw(2) << "ROC " << iroc << endl;

   int tbmch = iroc/8; // 0 or 1

   tb.Daq_Open( 65000, 1 );

   for( int row = 0; row < 80 ; ++row ) {

     for( int col = 0; col < 52; ++col ) {

   	tb.roc_Col_Enable( col, true );
   	tb.roc_Pix_Trim( col, row, 0 );
   	tb.roc_Pix_Cal( col, row, false );
     }


     tb.Daq_Start();
     tb.uDelay(100);

     tb.Pg_Single();
     tb.uDelay(200);

     tb.Daq_Stop();

     tb.roc_Chip_Mask();

    cout << setw(2) << row << ": DAQ size " << tb.Daq_GetSize( tbmch ) << endl;
   
     uint32_t remaining = 0;
     
     tb.Daq_Read( data, tbmch , 32000, remaining );

    for( int col = 0; col < 52; ++col )
         tb.roc_Pix_Mask( col, row );

     tb.roc_ClrCal();

     int size = data.size();

	for(int log = 0;log < size; log++)
	{
		
		Log.printf("%04x\n",data[log]);	  

	}

     uint32_t raw = 0;
     uint32_t raw2 = 0;
     uint32_t hdr = 0;
     uint32_t trl = 0;
     uint32_t roc = 0;
     size_t kroc = 0;

	int j = 0;

     for( int i = 0; i < size - 1; i++ ) {

                int x = data[i] & 0xffff;

                char s[40];
                HexToBin(x,s);
                printf("D %04X=%s\n", x,s);
	// Find ROC header
	if( i == 0) 
		j = 20;
	else
		j = 16;	
	
	while( j > 0) {
			
		uint32_t search = data[i];
		search <<= 16;
		search &= 0xffff0000;
	
		search += data[i+1];

		search &= (0xfff << j);

		search >>= j;

        //        int x = data[i] & 0xffff;
	//	int y = data[i+1] & 0xffff;
//                HexToBin(x,s);
//                printf("D %04X=%s", x,s);
//		HexToBin(y,s);
 ///               printf("%s\n",s);
//		HexToBin32(search,s);
  //              printf("S %08X=%s\n", search,s);
		
		switch(search)
		{
		  case 0x7ff: //TBM Trailer
			cout << "Trailer" << endl;
			trl++;
			break;
	
		  case 0x7fc: //TBM Header
			cout << "Header" << endl;
			raw = (data[i] << 16 ) + (data[i+1]);
			raw <<= (32-j); // Shift off header
		 	cout << "Evt: "<< ((raw & 0xff000000) >> 24) << endl;	
			hdr++;
                        //Reset to next position
                          j = j - 16;
                          if(j<0)
                          {
                                i++;
                                j+=16;
                          }      
                        if(i > size)
                                break;
  
			break;
			
		  case 0x7f8: //ROC Header
		  case 0x7f9:
		  case 0x7fa:
		  case 0x7fb:
			cout << "Roc : " << j << endl;
			roc++;
			while(1) {
			  raw = (data[i] << 16 ) + (data[i+1]);
 //                         HexToBin32(raw,s);
 //                         printf("1 %08X=%s\n", raw,s);
			  raw <<= (32-j); // Shift off header

 //                         HexToBin32(raw,s);
  //                        printf("3 %08X=%s\n", raw,s);

		 	  raw2 = (data[i+2] << 16) + data[i+3];
//                          HexToBin32(raw2,s);
  //                        printf("2 %08X=%s\n", raw2,s);

			  raw += raw2 >> j;

			  HexToBin32(raw,s);
                	  printf("RAW %08X=%s\n", raw & 0xFF800000,s);

			  if(
				((raw & 0xFF800000) == 0x7F800000) || 
				((raw & 0xFF800000) == 0x7F900000) || 
				((raw & 0xFF800000) == 0x7FA00000) || 
				((raw & 0xFF800000) == 0x7FB00000) || 
				((raw & 0xFF800000) == 0xFF800000))
			  {	
				cout << " No Pixel Data " << endl;
				break;
			  } 
			  raw >>= 8;		
			  cout << "Pixel data=" << raw << endl;

			  //Decode pixel data
			int ph = 0;
	        	int c = 0;
	        	int r = 0;
        		int x = 0;
       			int y = 0;

			ph = (raw & 0x0f) + ((raw >> 1) & 0xf0);
            		raw >>= 9;
            		c =    (raw >> 12) & 7;
            		c = c*6 + ((raw >>  9) & 7);
            		r =    (raw >>  6) & 7;
            		r = r*6 + ((raw >>  3) & 7);
            		r = r*6 + ( raw        & 7);
            		y = 80 - r/2;
            		x = 2*c + (r&1);
	                if( y > -1 && y < 80 && x > -1 && x < 52 )
                        modph[iroc][x][y] = ph;
	
			cout << "x: "<< x << "  y: " << y << "  ph: " << ph << endl;
			  //Reset to next position
			  i++;
			  j = j - 8;
			  if(j<0)
			  {
			  	i++;
				j+=16;
			  }	  
			if(i > size)
				break;			  
			} 
			break;
		}
		j--;
	} //search 
/*
        int d = data[i] & 0xf; // 4 bits data
        int q = (data[i]>>4) & 0xf; // 4 flag bits
        uint32_t ph = 0;
        int c = 0;
        int r = 0;
        int x = 0;
        int y = 0;
        switch (q)
          {
          case  0: break;

            // pixel data:
          case  1: raw = d; break;
          case  2: raw = (raw<<4) + d; break;
          case  3: raw = (raw<<4) + d; break;
          case  4: raw = (raw<<4) + d; break;
          case  5: raw = (raw<<4) + d; break;
          case  6: raw = (raw<<4) + d;
            npx[iroc]++;
            //DecodePixel(raw);
            ph = (raw & 0x0f) + ((raw >> 1) & 0xf0);
            raw >>= 9;
            c =    (raw >> 12) & 7;
            c = c*6 + ((raw >>  9) & 7);
            r =    (raw >>  6) & 7;
            r = r*6 + ((raw >>  3) & 7);
            r = r*6 + ( raw        & 7);
            y = 80 - r/2;
            x = 2*c + (r&1);
            if( y > -1 && y < 80 && x > -1 && x < 52 )
              modph[iroc][x][y] = ph;
            break;

*/	
  } //data

 cout << "Tbm Hdr: " << hdr << endl;
 cout << "Tbm Trl: " << trl << endl;
 cout << "Roc Hdr: " << roc << endl;


  } //row

 } //roc

 //Make plots

 TH2I * hROCS[8];
 TFile f("histos.root","recreate");
 
 for(int a = 0 ; a < 16 ; a++)
 {
	char szRoc[10];
	sprintf(szRoc,"hROC%d",a);
	hROCS[a] = new TH2I(szRoc,szRoc,52,0,52,80,0,80);

	cout << "Init Histos" << hROCS[a] << endl;
	
   	for(int col = 0 ; col < 52; col++)
		for(int row = 0; row < 80 ; row++)
		{
			hROCS[a]->Fill(col,row,modph[a][col][row]);
		} 
	//TCanvas c1;
	//c1.cd();
	f.cd();
	hROCS[a]->SetDrawOption("colz");
	hROCS[a]->Draw();
	//hROCS[a]->Write();

 }
 f.Write();
 f.Close();
 

}

CMD_PROC(errortest)
{

    std::vector<uint16_t> data;
    int iterations,roc;
 
    PAR_INT(iterations,1,10000000);
    PAR_INT(roc,0,15);

       for(int x = 0 ; x < 16 ; x++)
    {
	tb.roc_I2cAddr(x);
        tb.roc_Chip_Mask();
        tb.roc_ClrCal();
    }
	tb.roc_I2cAddr(roc);
	
    	for(int row = 30 ; row < 40 ; row++)
    	{

	    for(int col = 20 ; col < 25 ; col++) 

	    {

	       tb.roc_Col_Enable( col, true );
       	       tb.roc_Pix_Trim( col, row, 0 );
               tb.roc_Pix_Cal( col, row, false );
            }
	}

     uint32_t pixels = 0;
     uint32_t errors = 0;

     int tbm = roc/8;
     cout << "Open TBM: " << tbm << endl;
     tb.Daq_Open(1000000,tbm);

     tb.Daq_Start();
     tb.uDelay(100);

     for(int it = 0 ; it < iterations ; it ++)
     {
	tb.Pg_Single();
     	tb.uDelay(500);

	uint32_t remaining = 0;

        tb.Daq_Read( data, tbm , 32000, remaining );

        int size = data.size();

	cout << "read "<< size << "  remaining " << remaining << endl;

	std::vectorR<int> iroc, row, col, ph;

	uint16_t num = ParseData(data,tbm ,iroc,row,col,ph);

	cout << "parsed " << num <<endl;

	data.clear();

	// Check data
	pixels += iroc.size();

	for(int c = 0 ; c < iroc.size() ; c++)
	{
	    int y = row[c];
	    int x = col[c];
	    
	    int exp_x = floor(c / 10) + 20;

	    int exp_y = (((exp_x%2)==1)? 39 - (c % 10) :(c % 10) + 30);


	    if(x != exp_x || y != exp_y)
	    {
		cout << x << "   " << y << endl;
		cout << "Exp: " << exp_x<< " , " << exp_y << endl;
		errors++;
	    }
		 
	}

	iroc.clear();
	row.clear();
	col.clear();
	ph.clear();

     }
     tb.roc_ClrCal();

     tb.Daq_Stop();

     tb.Daq_Close();

     cout << "Pixels " << pixels << endl << "Errors " << errors << endl;

}

int ParseData( std::vector<uint16_t> &data, int tbmch ,std::vectorR<int> & iroc, std::vectorR<int> & row, std::vectorR<int> & col, std::vectorR<int> & ph)
{
     uint32_t raw = 0;
     uint32_t raw2 = 0;
     uint32_t hdr = 0;
     uint32_t trl = 0;
     uint32_t roc = 0;
     size_t kroc = 0;
 	cout << "tbm: " << tbmch << endl;
        int j = 0;
     uint16_t size = data.size();

     for( int i = 0; i < size - 1; i++ ) {

                int x = data[i] & 0xffff;

                char s[40];
                HexToBin(x,s);
                printf("D %04X=%s\n", x,s);
        // Find ROC header
        if( i == 0)
                j = 20;
        else
                j = 16;

        while( j > 0) {

                uint32_t search = data[i];
                search <<= 16;
                search &= 0xffff0000;

                search += data[i+1];

                search &= (0xfff << j);

                search >>= j;

                switch(search)
                {
                  case 0x7ff: //TBM Trailer
                        cout << "Trailer" << endl;
                        trl++;
                        break;

                  case 0x7fc: //TBM Header
                        cout << "Header" << endl;
                        raw = (data[i] << 16 ) + (data[i+1]);
                        raw <<= (32-j); // Shift off header
                        cout << "Evt: "<< ((raw & 0xff000000) >> 24) << endl;
                        hdr++;
                        //Reset to next position
                          j = j - 16;
                          if(j<0)
                          {
                                i++;
                                j+=16;
                          }
                        if(i > size)
                                break;

                        break;

                  case 0x7f8: //ROC Header
                  case 0x7f9:
                  case 0x7fa:
                  case 0x7fb:
                        cout << "Roc : " << j << endl;
                        roc++;
                        while(1) {
                          raw = (data[i] << 16 ) + (data[i+1]);
                          raw <<= (32-j); // Shift off header

                          raw2 = (data[i+2] << 16) + data[i+3];

                          raw += raw2 >> j;

                          HexToBin32(raw,s);
                          printf("RAW %08X=%s\n", raw & 0xFF800000,s);

                          if(
                                ((raw & 0xFF800000) == 0x7F800000) ||
                                ((raw & 0xFF800000) == 0x7F900000) ||
                                ((raw & 0xFF800000) == 0x7FA00000) ||
                                ((raw & 0xFF800000) == 0x7FB00000) ||
                                ((raw & 0xFF800000) == 0xFF800000))
                          {
                                cout << " No Pixel Data " << endl;
                                break;
                          }
                          raw >>= 8;
                          cout << "Pixel data=" << raw << endl;

                          //Decode pixel data
                        int pulse = 0;
                        int c = 0;
                        int r = 0;
                        int x = 0;
                        int y = 0;

                        pulse = (raw & 0x0f) + ((raw >> 1) & 0xf0);
                        raw >>= 9;
                        c =    (raw >> 12) & 7;
                        c = c*6 + ((raw >>  9) & 7);
                        r =    (raw >>  6) & 7;
                        r = r*6 + ((raw >>  3) & 7);
                        r = r*6 + ( raw        & 7);
                        y = 80 - r/2;
                        x = 2*c + (r&1);
                        
			iroc.push_back(tbmch?roc+7:roc);
			row.push_back(y);
			col.push_back(x);
			ph.push_back(pulse);
		
                        cout << "x: "<< x << "  y: " << y << "  ph: " << pulse << endl;
                          //Reset to next position
                          i++;
                          j = j - 8;
                          if(j<0)
                          {
                                i++;
                                j+=16;
                          }
                        if(i > size)
                                break;
                        }
                        break;
                } //switch
		j--;
	    } //while
	} //data
	return iroc.size();
}

CMD_PROC(test)
{

	if (settings.port_prober >= 0)
	{
		test_wafer();
	}
	else
	{
		char id[42];
		PAR_STRINGEOL(id,40);
		test_chip(id);
	}

//	FILE *f = fopen("g_chipdata.txt", "wt");
//	if (f) { g_chipdata.Save(f);  fclose(f); }

	return true;
}


#define CSX   8050
#define CSY  10451

const int CHIPOFFSET[4][4][2] =
{	// from -> to  0           1           2           3
	/*   0  */ { {   0,   0},{-CSX,   0},{   0,-CSY},{-CSX,-CSY} },
	/*   1  */ { { CSX,   0},{   0,   0},{ CSX,-CSY},{   0,-CSY} },
	/*   2  */ { {   0, CSY},{-CSX, CSY},{   0,   0},{-CSX,   0} },
	/*   3  */ { { CSX, CSY},{   0, CSY},{ CSX,   0},{   0,   0} },
};

bool ChangeChipPos(int pos)
{
	int rsp;
	char *answer = prober.printf("MoveChuckSeparation");
	if (sscanf(answer, "%i", &rsp)!=1) rsp = -1;
	if (rsp != 0) { printf(" RSP %s\n", answer); return false; }

	int x = CHIPOFFSET[chipPos][pos][0];
	int y = CHIPOFFSET[chipPos][pos][1];

	answer = prober.printf("MoveChuckPosition %i %i H", x, y);
	if (sscanf(answer, "%i", &rsp)!=1) rsp = -1;
	if (rsp != 0) { printf(" RSP %s\n", answer); return false; }

	answer = prober.printf("SetMapHome");
	if (sscanf(answer, "%i", &rsp)!=1) rsp = -1;
	if (rsp != 0) { printf(" RSP %s\n", answer); return false; }

	chipPos = pos;
	return true;
}


CMD_PROC(chippos)
{
	char s[4];
	PAR_STRING(s,2);
	if (s[0] >= 'a') s[0] -= 'a' - 'A';
	if (s[0] == 'B') return true; // chip B not existing

	int i;
	for (i=0; i<4; i++)
	{
		if (s[0] == chipPosChar[i])
		{
			ChangeChipPos(i);
			return true;
		}
	}
	return true;
}


CDefectList deflist[4];


bool goto_def(int i)
{
	int x, y;
	if (!deflist[chipPos].get(i, x, y)) return false;
	char *answer = prober.printf("StepNextDie %i %i", x, y);

	int rsp;
	if (sscanf(answer, "%i", &rsp)!=1) rsp = -1;
	if (rsp!=0) printf(" RSP %s\n", answer);

	return rsp == 0;
}


bool go_TestDefects()
{
	if (deflist[chipPos].size() == 0) return true;

	printf(" Begin Defect Chip %c Test\n", chipPosChar[chipPos]);

	// goto first position
	int i = 0;
	if (!goto_def(i)) return false;

	prober.printf("MoveChuckContact");

	do
	{
		int x, y;
		g_chipdata.Invalidate();

		if (!ReportChip(x,y)) break;
		GetTimeStamp(g_chipdata.startTime);
		Log.timestamp("BEGIN");
		bool repeat;
		int bin = test_roc_dig(repeat);
		GetTimeStamp(g_chipdata.endTime);
		Log.timestamp("END");
		Log.puts("\n");
		Log.flush();
		printf("%3i\n", bin);
		prober.printf("BinMapDie %i", bin);

		if (keypressed())
		{
			printf(" wafer test interrupted!\n");
			break;
		}

		tb.mDelay(100);
		i++;
	} while (goto_def(i));

	prober.printf("MoveChuckSeparation");

	return true;
}


bool TestSingleChip(int &bin, bool &repeat)
{
	int x, y;
	g_chipdata.Invalidate();

	if (!ReportChip(x,y)) return false;
	GetTimeStamp(g_chipdata.startTime);
	Log.timestamp("BEGIN");
	tb.SetLed(0x10);
	bin = test_roc_dig(repeat);
	tb.SetLed(0x00);
	tb.Flush();

	//		if (0<bin && bin<13) deflist[chipPos].add(x,y);
	GetTimeStamp(g_chipdata.endTime);
	Log.timestamp("END");
	Log.puts("\n");
	Log.flush();
	printf("%3i\n", bin);
	return true;
}


bool go_TestChips()
{
	printf(" Begin Chip %c Test\n", chipPosChar[chipPos]);
	prober.printf("MoveChuckContact");
	tb.mDelay(200);

	while (true)
	{
		int bin = 0;
		bool repeat;
		if (!TestSingleChip(bin,repeat)) break;

		int nRep = settings.errorRep;
		if (nRep > 0 && repeat)
		{
			prober.printf("BinMapDie %i", bin);
			prober.printf("MoveChuckSeparation");
			tb.mDelay(100);
			prober.printf("MoveChuckContact");
			tb.mDelay(200);
			if (!TestSingleChip(bin,repeat)) break;
			nRep--;
		}

		if (keypressed())
		{
			prober.printf("BinMapDie %i", bin);
			printf(" wafer test interrupted!\n");
			break;
		}

		// prober step
		int rsp;
		char *answer = prober.printf("BinStepDie %i", bin);
		if (sscanf(answer, "%i", &rsp)!=1) rsp = -1;
		if (rsp != 0) printf(" RSP %s\n", answer);
		tb.mDelay(100);

		// last chip ?
		if (rsp == 0)   // ok -> next chip
			continue;
		if (rsp == 703) // end of wafer -> return
		{
			prober.printf("MoveChuckSeparation");
			return true;
		}

		printf(" prober error! test stopped\n");
		break;
	}

	prober.printf("MoveChuckSeparation");
	return false;
}


CMD_PROC(go)
{
	static bool isRunning = false;

	char s[12];
	if (PAR_IS_STRING(s, 10))
	{
		if (strcmp(s,"init") == 0) { isRunning = false; }
		else if (strcmp(s,"cont") == 0) { isRunning = true; }
		else { printf(" illegal parameter");  return true; }
	}

	if (!isRunning)
	{
		ChangeChipPos(0);
		for (int k=0; k<4; k++) deflist[k].clear();
		prober.printf("StepFirstDie");
		isRunning = true;
	}

	printf(" wafer test running\n");
	if (!ReportWafer()) return true;

	while (true)
	{
		// test chips
		if (!go_TestChips()) break;

		// test defect chips
		prober.printf("StepFirstDie");
		if (!go_TestDefects()) break;

		// next chip position
		if (chipPos < 3)
		{
			if (chipPos != 0) // exclude chip B (1)
			{
				if (!ChangeChipPos(chipPos+1)) break;
			}
			else
			{
				if (!ChangeChipPos(chipPos+2)) break;
			}
			char *answer = prober.printf("StepFirstDie");
			int rsp;
			if (sscanf(answer, "%i", &rsp)!=1) rsp = -1;
			if (rsp != 0)
			{
				printf(" RSP %s\n", answer);
				break;
			}
		}
		else
		{
			ChangeChipPos(0);
			isRunning = false;
			break;
		}
	}
	return true;
}


CMD_PROC(first)
{
	printf(" RSP %s\n", prober.printf("StepFirstDie"));
	return true;
}


CMD_PROC(next)
{
	printf(" RSP %s\n", prober.printf("StepNextDie"));
	return true;
}


CMD_PROC(goto)
{
	int x, y;
	PAR_INT(x, -100, 100);
	PAR_INT(y, -100, 100);

	char *msg = prober.printf("StepNextDie %i %i", x, y);
	printf(" RSP %s\n", msg);
	return true;
}




// =======================================================================


void cmdHelp()
{
	if (settings.port_prober >= 0)
	{
	 fputs("\n"
	 "+-- control commands ------------------------------------------+\n"
	 "| h                  display this text                         |\n"
	 "| exit               exit commander                            |\n"
	 "+-- wafer test ------------------------------------------------+\n"
	 "| go                 start wafer test (press <cr> to stop)     |\n"
	 "| test               run chip test                             |\n"
	 "| pr <command>       send command to prober                    |\n"
	 "| sep                prober z-axis separation                  |\n"
	 "| contact            prober z-axis contact                     |\n"
	 "| first              go to first die and clear wafer map       |\n"
	 "| next               go to next die                            |\n"
	 "| goto <x> <y>       go to specifed die                        |\n"
	 "| chippos <ABCD>     move to chip A, B, C or D                 |\n"
	 "+--------------------------------------------------------------+\n",
	 stdout);
	}
	else
	{
	 fputs("\n"
	 "+-- control commands ------------------------------------------+\n"
	 "| h                  display this text                         |\n"
	 "| exit               exit commander                            |\n"
	 "+-- chip test -------------------------------------------------+\n"
	 "| test <chip id>     run chip test                             |\n"
	 "+--------------------------------------------------------------+\n",
	 stdout);
	}
}


CMD_PROC(h)
{
	cmdHelp();
	return true;
}



void cmd()
{
	// --- connection, startup commands ----------------------------------
	CMD_REG(scan,     "scan                          enumerate USB devices");
	CMD_REG(open,     "open <name>                   open connection to testboard");
	CMD_REG(close,    "close                         close connection to testboard");
	CMD_REG(welcome,  "welcome                       blinks with LEDs");
	CMD_REG(setled,   "setled                        set atb LEDs"); //give 0-15 as parameter for four LEDs
	CMD_REG(log,      "log <text>                    writes text to log file");
	CMD_REG(upgrade,  "upgrade <filename>            upgrade DTB");
	CMD_REG(rpcinfo,  "rpcinfo                       lists DTB functions");
	CMD_REG(ver,      "ver                           shows DTB software version number");
	CMD_REG(version,  "version                       shows DTB software version");
	CMD_REG(info,     "info                          shows detailed DTB info");
	CMD_REG(boardid,  "boardid                       get board id");
	CMD_REG(init,     "init                          inits the testboard");
	CMD_REG(flush,    "flush                         flushes usb buffer");
	CMD_REG(clear,    "clear                         clears usb data buffer");
	CMD_REG(hvon,     "hvon                          switch HV on");
	CMD_REG(hvoff,    "hvoff                         switch HV off");
	CMD_REG(reson,    "reson                         activate reset");
	CMD_REG(resoff,   "resoff                        deactivate reset");
	CMD_REG(status,   "status                        shows testboard status");
	CMD_REG(rocaddr,  "rocaddr                       set ROC address");


	// --- delay commands ------------------------------------------------
	CMD_REG(udelay,   "udelay <us>                   waits <us> microseconds");
	CMD_REG(mdelay,   "mdelay <ms>                   waits <ms> milliseconds");

	// --- test board commands -------------------------------------------
	CMD_REG(clk,      "clk <delay>                   clk delay");
	CMD_REG(ctr,      "ctr <delay>                   ctr delay");
	CMD_REG(sda,      "sda <delay>                   sda delay");
	CMD_REG(tin,      "tin <delay>                   tin delay");
	CMD_REG(clklvl,   "clklvl <level>                clk signal level");
	CMD_REG(ctrlvl,   "ctrlvl <level>                ctr signel level");
	CMD_REG(sdalvl,   "sdalvl <level>                sda signel level");
	CMD_REG(tinlvl,   "tinlvl <level>                tin signel level");
	CMD_REG(clkmode,  "clkmode <mode>                clk mode");
	CMD_REG(ctrmode,  "ctrmode <mode>                ctr mode");
	CMD_REG(sdamode,  "sdamode <mode>                sda mode");
	CMD_REG(tinmode,  "tinmode <mode>                tin mode");

	CMD_REG(sigoffset,"sigoffset <offset>            output signal offset");
	CMD_REG(lvds,     "lvds                          LVDS inputs");
	CMD_REG(lcds,     "lcds                          LCDS inputs");

	CMD_REG(pon,      "pon                           switch ROC power on");
	CMD_REG(poff,     "poff                          switch ROC power off");
	CMD_REG(va,       "va <mV>                       set VA in mV");
	CMD_REG(vd,       "vd <mV>                       set VD in mV");
	CMD_REG(ia,       "ia <mA>                       set IA in mA");
	CMD_REG(id,       "id <mA>                       set ID in mA");

	CMD_REG(getva,    "getva                         set VA in V");
	CMD_REG(getvd,    "getvd                         set VD in V");
	CMD_REG(getia,    "getia                         set IA in mA");
	CMD_REG(getid,    "getid                         set ID in mA");

	CMD_REG(d1,       "d1 <signal>                   assign signal to D1 output");
	CMD_REG(d2,       "d2 <signal>                   assign signal to D2 outout");
	CMD_REG(a1,       "a1 <signal>                   assign analog signal to A1 output");
	CMD_REG(a2,       "a2 <signal>                   assign analog signal to A2 outout");
	CMD_REG(probeadc, "probeadc <signal>             assign analog signal to ADC");

	CMD_REG(pgset,    "pgset <addr> <bits> <delay>   set pattern generator entry");
	CMD_REG(pgstop,   "pgstop                        stops pattern generator");
	CMD_REG(pgsingle, "pgsingle                      send single pattern");
	CMD_REG(pgtrig,   "pgtrig                        enable external pattern trigger");
	CMD_REG(pgloop,   "pgloop <period>               start patterngenerator in loop mode");

	CMD_REG(dopen,    "dopen <buffer size>           Open DAQ and allocate memory");
	CMD_REG(dclose,   "dclose                        Close DAQ");
	CMD_REG(dstart,   "dstart                        Enable DAQ");
	CMD_REG(dstop,    "dstop                         Disable DAQ");
	CMD_REG(dsize,    "dsize <channel>               Show DAQ buffer fill state");
	CMD_REG(dread,    "dread                         Read Daq buffer and show as digital data");
	CMD_REG(dreada,   "dreada                        Read Daq buffer and show as analog data");
	CMD_REG(showclk,  "showclk                       show CLK signal");
	CMD_REG(showctr,  "showctr                       show CTR signal");
	CMD_REG(showsda,  "showsda                       show SDA signal");
	CMD_REG(takedata, "takedata                      Continous DTB readout (to stop press any key)");
	CMD_REG(takedata2,"takedata2                     Continous DTB readout and decoding");
	CMD_REG(deser,    "deser <value>                 controls deser160");

	CMD_REG(adcena,   "adcena                        enable ADC");
	CMD_REG(adcdis,   "adcdis                        disable ADC");

	// --- ROC commands --------------------------------------------------
	CMD_REG(select,   "select <addr range>           set i2c address");
	CMD_REG(dac,      "dac <address> <value>         set DAC");
	CMD_REG(vana,     "vana <value>                  set Vana");
	CMD_REG(vtrim,    "vtrim <value>                 set Vtrim");
	CMD_REG(vthr,     "vthr <value>                  set VthrComp");
	CMD_REG(vcal,     "vcal <value>                  set Vcal");
	CMD_REG(wbc,      "wbc <value>                   set WBC");
	CMD_REG(ctl,      "ctl <value>                   set control register");
	CMD_REG(cole,     "cole <range>                  enable column");
	CMD_REG(cold,     "cold <range>                  disable columns");
	CMD_REG(pixe,     "pixe <range> <range> <value>  trim pixel");
	CMD_REG(pixd,     "pixd <range> <range>          kill pixel");
	CMD_REG(cal,      "cal <range> <range>           calibrate pixel");
	CMD_REG(cals,     "cals <range> <range>          sensor calibrate pixel");
	CMD_REG(cald,     "cald                          clear calibrate");
	CMD_REG(mask,     "mask                          mask all pixel and cols");

	// --- experimental commands --------------------------------------------
	//CMD_REG(ethsend,  "ethsend <string>              send <string> in a Ethernet packet");
	//CMD_REG(ethrx,    "ethrx                         shows number of received packets");
	CMD_REG(shmoo,    "shmoo vx xrange vy ymin yrange");
	CMD_REG(phscan,   "phscan                        ROC pulse height scan");
	CMD_REG(readback, "readback                      read out ROC data");
	CMD_REG(deser160, "deser160                      allign deser160");
	CMD_REG(tbmregs, "tbmregs                        print tbm registers");
	CMD_REG(tbmsel, "tbmsel <port> <hub>             select tbm");
	CMD_REG(modscan, "modscan                      module scan");

	CMD_REG(tbmgetraw, "tbmgetraw <reg>			get TBM register");
	CMD_REG(tbmget, "tbmget <reg>			get TBM register");
	CMD_REG(tbmset, "tbmset <reg> <value>		set TBM register");
	CMD_REG(modsel, "modsel 					");
	CMD_REG(trimroc, "trimroc 					");
	CMD_REG(modalive, "modalive 					");
	CMD_REG(errortest, "errortest <iterations> 			");


	// --- chip test command ---------------------------------------------
	if (settings.port_prober >= 0)
	CMD_REG(test,     "test                          run chip test");
	else
	CMD_REG(test,     "test <chip id>                run chip test");

	if (settings.port_prober >= 0)
	{
	// --- prober commands -----------------------------------------------
	CMD_REG(go,       "go init|cont                  start wafer test (press <cr> to stop)");
	CMD_REG(pr,       "pr <command>                  send command to prober");
	CMD_REG(sep,      "sep                           prober z-axis separation");
	CMD_REG(contact,  "contact                       prober z-axis contact");
	CMD_REG(first,    "first                         go to first die and clear wafer map");
	CMD_REG(next,     "next                          go to next die");
	CMD_REG(goto,     "goto                          go to specified die");
	CMD_REG(chippos,  "chippos <ABCD>                move to chip A, B, C or D");
	}

	CMD_REG(h,        "h                             simple help");

	cmdHelp();

	cmd_intp.SetScriptPath(settings.path);

	// command loop
	while (true)
	{
		try
		{
			CMD_RUN(stdin);
			return;
		}
		catch (CRpcError e)
		{
			e.What();
		}
	}
}

