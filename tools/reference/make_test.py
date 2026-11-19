import sys

#id, timestamp, channel name, value
LINES = [
b"1,1009843200000,channel_41,0.25",             #ok
b"2,1009843230000,channel_41,-1.5e2",           #ok
b"3,1009843260000,channel_41,",                 #empty: no value
b"4,1009843290000,channel_41,nan",              #nan
b"5,1009843320000,channel_41,-INF",             #nan: infinity
b"6,1009843350000,channel_41,1e400",            #nan: overflows to inf
b"7,1009843380000,channel_41",                  #malformed: 3 fields
b"8,1009843410000,channel_41,1.0,x",            #malformed: 5 fields
b"9,abc,channel_41,1.0",                        #malformed: bad timestamp
b"10,1009843470000,channel_41, 1.0",            #malformed: space
b"11,1009843500000,channel_41,+1.0",            #malformed: plus sign
b"12,1009843530000,channel_41,.5",              #malformed: no leading digit
b"abc,1009843560000,channel_41,",               #malformed: bad id beats empty
b"14,1009843590000,channel 41,2.0",             #malformed: space in channel
b"",                                            #ignored: blank line
b"16,1009843650000,channel_41,1_000",           #malformed: underscore
b"17,1009843680000,channel_41,3.0\r",           #malformed: carriage return
b"18,1009843710000,channel_41,4.0",             #malformed: no trailing newline
]

out = sys.argv[1] if len(sys.argv) > 1 else "test.csv"
with open(out, "wb") as f:
    f.write(b"\n".join(LINES))