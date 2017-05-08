To compile the server:
	g++ -pthread server-mt.c -o server-mt
To run the server:
	./server-mt PORT_NO WORKER_THREADS QUEUE_SIZE
====================================

Experimental Setup:
Setup of experiment
We used a router (as a switch) to connect 2 laptop devices using 2 lan cables:
• 1 st lan cable from 1 st laptop to switch
• 2 nd lan cable from 2 nd laptop to switch
Hardware specification of client:
• CPU – Intel® CoreTM i7-4510U CPU @ 2.00GHz × 4
• Memory - 7.7 GiB
• OS Type – 64-bit
• Disk - 976.0 GB
Hardware specification of server:
• CPI - Intel® CoreTM i7-4500U CPU @ 1.80GHz × 4
• Memory – 7.7 GiB
• OS Type – 64-bit
• Disk – 76.5 GB

====================================

Sample results from test run:
For Part A
	N(#worker threads)		Throughput(Files served/sec)
		1 								5.878
		2 								5.879
		3 								5.879
		4 								5.879
		5 								5.879
		6 								5.879
		7 								5.879
		8 								5.879
		9 								5.879
		10 								5.879
		
For Part B
	n = 1
Total Successful Requests: 620 
Throughput: 2.846

n = 2
Total Successful Requests: 691 
Throughput: 3.509

n = 3
Total Successful Requests: 747 
Throughput: 4.086

n = 4
Total Successful Requests: 816 
Throughput: 4.282

n = 5
Total Successful Requests: 812 
Throughput: 4.364

n = 6
Total Successful Requests: 812 
Throughput: 4.533

n = 7
Total Successful Requests: 803 
Throughput: 4.435

n = 8
Total Successful Requests: 838 
Throughput: 4.191

n = 9
Total Successful Requests: 811 
Throughput: 4.004

n = 10
Total Successful Requests: 836 
Throughput: 3.674

n = 100
Total Successful Requests: 865 
Throughput: 3.882