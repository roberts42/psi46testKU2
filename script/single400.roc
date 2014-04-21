 dac   1 8   // Vdig
 dac  2  120
 dac  3  40    // Vsf
 dac  4  12    // Vcomp

 dac  7  60    // VwllPr
 dac  9  60    // VwllSh
 dac 10 117    // VhldDel
 dac 11  40    // Vtrim
 dac 12  20    // VthrComp

 dac 13  30    // VIBias_Bus
 dac 14   6    // Vbias_sf
 dac 22  99    // VIColOr

 dac 15  40    // VoffsetOp
 dac 17  80    // VoffsetRO
 dac 18 115    // VIon

 dac 19 100    // Vcomp_ADC
 dac 20  90    // VIref_ADC

 dac 25   2    // Vcal
 dac 26  68  // CalDel

 dac $fe 45   // WBC
 dac $fd  4   // CtrlReg


 pgstop
 pgset 0 b010000 20
 pgset 1 b000000 0
 pgsingle


 pgset 0 b010000 15
 pgset 1 b000100 50
 pgset 2 b100010  0

 pgloop 20000
 flush

 dopen 50000
 dstart
 udelay 100
 dstop
 dsize 0
 dsize 1

 


