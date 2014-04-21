log === startdigmod ===

--- set voltages ----------------------------
vd 2500 mV
id  800 mA
va 1600 mV
ia  800 mA

--- setup timing & levels -------------------
clk  4
sda 19  (CLK + 15)
ctr  4  (CLK + 0)
tin  9  (CLK + 5)

clklvl 15
ctrlvl 15
sdalvl 15
tinlvl 15

--- power on --------------------------------
pon
mdelay 400
resoff
mdelay 200

getid
getia


a1 1
udelay 100

a2 3
udelay 100

--- setup TBM -------------------------------
modsel b11111
modsel 0

tbmset $E4 $F0    Init TBM, Reset ROC
tbmset $F4 $F0
tbmset $E0 $00    Disable PKAM Counter
tbmset $F0 $00
tbmset $E2 $C0    Mode = Calibration
tbmset $F2 $C0
tbmset $E8 $02    Set PKAM Counter
tbmset $F8 $02
tbmset $EA $C0 Delays (?)
tbmset $FA $C0
tbmset $EC $00    Temp measurement control
tbmset $FC $00


mdelay 100

--- setup probe outputs ---------------------
a2 1  sdata
udelay 500
a1 3  ctr
udelay 500

--- setup readout timing --------------------
pgstop
pgset 0 b010000  20  pg_rest
pgset 1 b000000   0
pgsingle

pgset 0 b001000  15  pg_rest
pgset 1 b000100  50  pg_cal
pgset 2 b100010   0  pg_trg pg_sync

--- pgloop 20000

flush
