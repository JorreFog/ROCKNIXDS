import os, fcntl, struct, time
UI_SET_EVBIT=0x40045564; UI_SET_KEYBIT=0x40045565; UI_DEV_CREATE=0x5501
EV_SYN,EV_KEY=0,1
KEYS={"enter":28,"esc":1,"up":103,"down":108,"left":105,"right":106,"space":57,"bslash":43,"tab":15}
for i,ch in enumerate("qwertyuiop"): KEYS[ch]=16+i
for i,ch in enumerate("asdfghjkl"): KEYS[ch]=30+i
for i,ch in enumerate("zxcvbnm"): KEYS[ch]=44+i
fd=os.open("/dev/uinput",os.O_WRONLY|os.O_NONBLOCK)
fcntl.ioctl(fd,UI_SET_EVBIT,EV_KEY)
for k in KEYS.values(): fcntl.ioctl(fd,UI_SET_KEYBIT,k)
dev=struct.pack("80sHHHHi",b"claude-vkbd",3,0x1234,0x5678,1,0)+b"\0"*(4*64*4)
os.write(fd,dev); fcntl.ioctl(fd,UI_DEV_CREATE)
def ev(c,v): os.write(fd,struct.pack("llHHi",0,0,EV_KEY,c,v)); os.write(fd,struct.pack("llHHi",0,0,EV_SYN,0,0))
fifo="/tmp/vkbd"
if not os.path.exists(fifo): os.mkfifo(fifo)
print("ready",flush=True)
while True:
    with open(fifo) as f:
        for line in f:
            for k in line.split():
                if k.startswith("hold:"):          # hold:<key>:<secs>
                    _,n,s=k.split(":"); ev(KEYS[n],1); time.sleep(float(s)); ev(KEYS[n],0); time.sleep(0.2)
                elif k.startswith("wait:"): time.sleep(float(k[5:]))
                else: ev(KEYS[k],1); time.sleep(0.08); ev(KEYS[k],0); time.sleep(0.35)
