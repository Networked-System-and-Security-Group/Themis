# -*- coding: utf-8 -*-
import sys
import random
import math
import heapq
from optparse import OptionParser
from custom_rand import CustomRand

class Flow:
	def __init__(self, src, dst, size, t):
		self.src, self.dst, self.size, self.t = src, dst, size, t
	def __str__(self):
		return "%d %d 3 100 %d %.9f"%(self.src, self.dst, self.size, self.t)

def translate_bandwidth(b):
	if b == None:
		return None
	if type(b)!=str:
		return None
	if b[-1] == 'G':
		return float(b[:-1])*1e9
	if b[-1] == 'M':
		return float(b[:-1])*1e6
	if b[-1] == 'K':
		return float(b[:-1])*1e3
	return float(b)

def poisson(lam):
	return -math.log(1-random.random())*lam

if __name__ == "__main__":
	port = 80
	parser = OptionParser()
	parser.add_option("-c", "--cdf", dest = "cdf_file", help = "the file of the traffic size cdf", default = "uniform_distribution.txt")
	parser.add_option("-n", "--nhost", dest = "nhost", help = "number of hosts")
	parser.add_option("-l", "--load", dest = "load", help = "the percentage of the traffic load to the network capacity, by default 0.3", default = "0.3")
	parser.add_option("-b", "--bandwidth", dest = "bandwidth", help = "the bandwidth of host link (G/M/K), by default 10G", default = "10G")
	parser.add_option("-t", "--time", dest = "time", help = "the total run time (s), by default 10", default = "10")
	parser.add_option("-o", "--output", dest = "output", help = "the output file", default = "tmp_traffic.txt")
	options,args = parser.parse_args()

	base_t = 2000000000

	if not options.nhost:
		print ("please use -n to enter number of hosts")
		sys.exit(0)
	nhost = int(options.nhost)
	load = float(options.load)
	bandwidth = translate_bandwidth(options.bandwidth)
	time = float(options.time)*1e9 # translates to ns
	output = options.output
	if bandwidth == None:
		print ("bandwidth format incorrect")
		sys.exit(0)

	fileName = options.cdf_file
	file = open(fileName,"r")
	lines = file.readlines()
	# read the cdf, save in cdf as [[x_i, cdf_i] ...]
	cdf = []
	for line in lines:
		x,y = map(float, line.strip().split(' '))
		cdf.append([x,y])

	# create a custom random generator, which takes a cdf, and generate number according to the cdf
	customRand = CustomRand()
	if not customRand.setCdf(cdf):
		print ("Error: Not valid cdf")
		sys.exit(0)

	ofile = open(output, "w")

	# generate flows
	avg = customRand.getAvg()
	intra_flow_cnt = 0
	inter_flow_cnt = 0
	# 按照cdf文件，得出流的平均size，按流平均size计算流发送的间隔
	avg_inter_arrival = 1/(bandwidth*load/8./avg)*1000000000
	n_flow_estimate = int(time / avg_inter_arrival * nhost)
	n_flow = 0
	ofile.write("%d \n"%n_flow_estimate)
	inter_flow_cnt_limit = n_flow_estimate // 6
	intra_flow_cnt_limit = n_flow_estimate - inter_flow_cnt_limit
	print(f"inter_flow_cnt_limit = {inter_flow_cnt_limit}")
	print(f"intra_flow_cnt_limit = {intra_flow_cnt_limit}")
	host_list = [(base_t + int(poisson(avg_inter_arrival)), i) for i in range(nhost)]
	heapq.heapify(host_list)
	while len(host_list) > 0:
		t,src = host_list[0]
		inter_t = int(poisson(avg_inter_arrival))
		dst = random.randint(32, nhost-1)
		
		if inter_flow_cnt == inter_flow_cnt_limit and src <= 31:
			src = random.randint(32, nhost-1)
		
		while (dst == src):
			dst = random.randint(32, nhost-1)

		
		# 不能再生成DC间的流量了
		while ((inter_flow_cnt == inter_flow_cnt_limit) and (src <= 31 and dst >= 32 or src >= 32 and dst <= 31)):
			dst = random.randint(32, nhost-1)
			while (dst == src):
				dst = random.randint(32, nhost-1)
		# 不能再生成DC内部的流量了
		while ((intra_flow_cnt == intra_flow_cnt_limit) and (src <= 31 and dst <= 31 or src >= 32 and dst >= 32)):
			dst = random.randint(32, nhost-1)
			while (dst == src):
				dst = random.randint(32, nhost-1)

		if (t + inter_t > time + base_t):
			heapq.heappop(host_list)
		else:
			size = int(customRand.rand())
			if size <= 0:
				size = 1
		
			if (src <= 31 and dst >= 32 or src >= 32 and dst <= 31):
				inter_flow_cnt += 1
			else:
				intra_flow_cnt += 1
			
			n_flow += 1
			if src == dst:
				print("src == dst!!!")

			if dst < 32 :
				print('dst < 32!')
			ofile.write("%d %d 3 100 %d %.9f\n"%(dst, src, size, t * 1e-9))
			heapq.heapreplace(host_list, (t + inter_t, src))

	print(f"intra_flow_count = {intra_flow_cnt}\n inter_flow_count = {inter_flow_cnt}\n")
	print(f"intra_flow_count / inter_flow_count = {intra_flow_cnt / inter_flow_cnt}")

	ofile.seek(0)
	ofile.write("%d" % n_flow)
	ofile.close()
