#!/usr/bin/env python3
import time, sys, telegram, subprocess

delay = 0

def message(m):
	global delay
	if delay == 0:
		telegram.send(m)
		delay = 1
		
def mbus_readout():
	global values, last_update

	path = ['Release/mbus-test']
	args=['-m', '-R', '-a 5']
#	for i in prim_num:
#		args.append('-a %d' % i)

	last_update = time.strftime("%d.%m %H:%M:%S")
	print(last_update, end='\t', flush=True)
	
	proc = subprocess.run(path + args, capture_output=True, text=True, timeout=10)

	if proc.returncode != 0:
		print('subprocess error: ' + proc.stderr, flush=True)
		message('subprocess error: ' + proc.stderr)
		return
	else:
		print (proc.returncode, flush=True)


if __name__ == "__main__":	
	print('Start:' + time.strftime("%d.%m %H:%M:%S"))

	try:
		while True:
			mbus_readout()
			time.sleep(60)
			if delay != 0:
				delay += 1
			if delay > 30:
				delay = 0
		
	except KeyboardInterrupt:
		sys.exit()
