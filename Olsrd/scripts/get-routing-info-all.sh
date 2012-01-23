#!/usr/bin/python

# usage: 
# ./scripts/get-routing-info-all.sh

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


def grab_info(devlist):
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
    sock.send(REQUEST_DEFAULT)
   
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


def print_csv(devlist, infolist):

  # vectorize info
  # v = [ [ [ 0 for z in range(2)] for y in range(2)] for x in range(2)]

  csvstr = ""

  tables = infolist[0].split("Table: ")[1:] # ignore HTTP header
  # print infolist[0]

  for t, table in enumerate(tables):
    tablelines = table.splitlines()
    tablename = tablelines[0]
    tablecols = tablelines[1].split("\t")

    # print "TableName:",tablename
    # print "TableCols:",tablecols
    csvstr += "\"Table: " + tablename + "\"\n"
    csvstr += "Device," + tablelines[1].replace("\t",",") + "\n"

    for i, info in enumerate(infolist):
      table = info.split("Table: ")[1+t].strip()
      contents = table.splitlines()[2:]

      if tablename == "Neighbors":
        contents = contents[1:] # skip "2hop interface address"
        if (contents):
          # content rows are not newline delimited
          contentvals = contents[0].split("\t")
          newcontents = []
          line = ""
          for c, val in enumerate(contentvals):
            line += val
            if ((c+1)%5 == 0): # 5 columns
              newcontents.append(line)
              line = ""
            else:
              line += ","
          contents = newcontents

      # print "Table:",table
      # print "Contents:",contents

      for c, content in enumerate(contents):
        contentvals = content.split("\t")
        # print "ContentVals:",contentvals
        csvstr += devlist[i] + "," + content.replace("\t",",") + "\n"

    csvstr += "\n\n"

  print "\n\n" + csvstr
  return csvstr


# main
devlist = get_devs()
print "Devices:", devlist

setup_devs(devlist)

infolist = grab_info(devlist)
# print infolist

csvstr = print_csv(devlist, infolist)

myfile = open("info.csv", "w")
myfile.write(csvstr)
myfile.close()

sys.exit(0)

