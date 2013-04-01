import sys
import time
import subprocess, threading
import multiprocessing

class Command(object):
	def __init__(self, cmd):
		self.cmd = cmd.split()
		self.process = None

	def run(self, timeout):
		def target():
			self.process = subprocess.Popen(self.cmd, stderr=subprocess.PIPE)
			self.process.communicate()

		thread = threading.Thread(target=target)
		thread.start()

		thread.join(timeout)
		if thread.is_alive():
			self.process.terminate()
			thread.join()
			return "Timeout"
		return self.process.returncode

def format_result(r):
	if r == 0:
		return "\033[0;40;32mOK.\033[0m"
	return "\033[0;40;31m" + str(r) + "\033[0m"

def worker(bc):
	bc = bc.strip()
	cmd = Command(satcmd + " " + bc)
	r = cmd.run(120)
	print >>sys.stderr, bc, "  ", format_result(r)
	return r

if __name__ == '__main__':
	pool = multiprocessing.Pool(multiprocessing.cpu_count() * 2)
	bcs = sys.argv[1:]
	if len(bcs) == 0:
		bcs = sys.stdin.readlines()
	ts = time.time()
	
	res = pool.map(worker, bcs)

	print "* Total files:", len(res)
	print "* Timeout:", sum((x == "Timeout") for x in res)
	print "* Failed:", sum((x < 0) for x in res)
	print "* Total time: %.1f sec" % (time.time() - ts)

