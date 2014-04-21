log === startdigmod ===


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


--- setup probe outputs ---------------------
a2 1  sdata
udelay 100
a1 3  ctr
udelay 100

--- setup readout timing --------------------
pgstop
pgset 0 b010000  20  pg_rest
pgset 1 b000000   0
pgsingle

pgset 0 b010000  15  pg_rest
pgset 1 b000100  50  pg_cal
pgset 2 b100010   0  pg_trg pg_sync

pgloop 20000

flush
