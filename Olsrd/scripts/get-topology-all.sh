#!/usr/bin/python

# usage: 
# ./scripts/get-topology-all.sh
import socket
import sys
import subprocess
 
# HOST = "www.google.com"
# GET = "/index.html"
# PORT = 80
 
HOST = "localhost"
REMOTE_PORT = 2006
LOCAL_PORT_MIN = 9000

REQUEST_NEIGHS = "/nei"
REQUEST_LINKS = "/lin"
REQUEST_ROUTES = "/rou"
REQUEST_HNAS = "/hna"
REQUEST_MIDS = "/mid"
REQUEST_TOPOLOGY = "/top"
	
REQUEST_GATEWAY	= "/gat"
REQUEST_CONFIG 	= "/con"
REQUEST_INTERFACES = "/int"
REQUEST_2HOP = "/2ho" # includes /nei info
	
# includes /nei and /lin
REQUEST_NEIGHBORS = "/neighbors"
	
# includes /nei, /lin, /rou, /hna, /mid, /top
REQUEST_ALL = "/all"
	
REQUEST_DEFAULT = \
  REQUEST_LINKS + REQUEST_ROUTES + REQUEST_HNAS + REQUEST_MIDS + REQUEST_TOPOLOGY + REQUEST_GATEWAY + REQUEST_INTERFACES + REQUEST_2HOP

def get_devs():
  # get all attached devices
  devstr = subprocess.check_output(["adb","devices"]).strip()
  devlist = devstr.splitlines()[1:] # ignore header

  # get device ids only
  for index, dev in enumerate(devlist):
    devlist[index] = dev.split("\t")[0]

  return devlist


def setup_devs(devlist):
  for index, dev in enumerate(devlist):
    # set up adb port forwarding to the olsrd txtinfo plugin
    # adb forward <local> <remote>
    subprocess.call(["adb","-s",dev,"forward","tcp:%s" % (LOCAL_PORT_MIN+index),"tcp:%s" % REMOTE_PORT])
  return


def grab_info(devlist,request):
  infolist = []
  for index, dev in enumerate(devlist):
    try:
      sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    except socket.error, msg:
      sys.stderr.write("[ERROR] %s\n" % msg[1])
      sys.exit(1)
   
    try:
      sock.connect((HOST, LOCAL_PORT_MIN+index))
    except socket.error, msg:
      sys.stderr.write("[ERROR] %s\n" % msg[1])
      sys.exit(2)
   
    # sock.send("GET %s HTTP/1.0\r\nHost: %s\r\n\r\n" % (GET, HOST))
    sock.send(request)
   
    data = sock.recv(1024)
    string = ""
    while len(data):
      string = string + data
      data = sock.recv(1024)
    sock.close()
   
    # print string
    infolist.append(string)

    # myfile = open("out.html", "w")
    # myfile.write(string)
    # myfile.close()

  return infolist


def get_addrs(devlist):
  addrlist = []

  interfaceslist = grab_info(devlist,REQUEST_INTERFACES)
  
  for i, info in enumerate(interfaceslist):
    tables = interfaceslist[i].split("Table: ")[1:] # ignore HTTP header
    table = tables[0]
    contents = table.splitlines()[2:-1]

    addrs = []
    for c, content in enumerate(contents):
      contentvals = content.split("\t")
      addr = contentvals[4]
      addrs.append(addr)

    addrlist.append(addrs)

  return addrlist


def get_mprs(devlist):
  mprlist = []

  neighslist = grab_info(devlist,REQUEST_NEIGHS)
  
  for i, info in enumerate(neighslist):
    tables = neighslist[i].split("Table: ")[1:] # ignore HTTP header
    table = tables[0]
    contents = table.splitlines()[2:-1]

    mprs = []
    for c, content in enumerate(contents):
      contentvals = content.split("\t")
      if (contentvals[2].strip() == "YES"):
        mprs.append(contentvals[0])

    mprlist.append(mprs)

  return mprlist


def print_topology_halfviz(devlist):
  halfvizstr = ""

  addrlist = get_addrs(devlist)
  mprlist = get_mprs(devlist)

  topologylist = grab_info(devlist,REQUEST_TOPOLOGY)

  for i, info in enumerate(topologylist):
    tables = topologylist[0].split("Table: ")[1:] # ignore HTTP header
    table = tables[0]
    contents = table.splitlines()[2:-1]

    # print "Device:",devlist[i]
    # print "Table:",table
    halfvizstr += "Device: " + devlist[i] + "\n"

    # identify this node
    halfvizstr += "; this node\n"
    for a, addr in enumerate(addrlist[i]):
      halfvizstr += addr + " {color: red}\n"

    # identify this node's MRPs
    halfvizstr += "; this node's MPRs\n"
    for m, mpr in enumerate(mprlist[i]):
      halfvizstr += mpr + " {color: blue}\n"

    halfvizstr += "; edges\n"
    for c, content in enumerate(contents):
      contentvals = content.split("\t")
      # print "Contentvals:",contentvals
      destaddr = contentvals[0]
      lasthopaddr = contentvals[1]
      halfvizstr += lasthopaddr + " -> " + destaddr + "\n"

    halfvizstr += "\n\n"
   
  return halfvizstr.strip()


# main
devlist = get_devs()
# print "Devices:", devlist

setup_devs(devlist)

halfvizstr = print_topology_halfviz(devlist)
print halfvizstr

sys.exit(0)

