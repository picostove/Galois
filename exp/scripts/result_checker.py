#### Script to check the output of algorithms:
### Author: Gurbinder Gill (gurbinder533@gmail.com)
### python script.py masterFile allfile*
### expects files in the follwing format:
###### nodeID nodeFieldVal
######## These are generated by Galois::Runtime::printOutput function.
### Requires python version > 2.7

import sys

def check_results(masterFile, otherFiles, offset):
  with open(masterFile) as mfile, open(otherFiles) as ofile:
    mfile.seek(offset)
    for line1, line2 in zip(mfile,ofile):
      split_line1 = line1.split(' ')
      split_line2 = line2.split(' ')
      offset = offset + len(line1);
      if (split_line1[0] == split_line2[0]):
        if(abs(float(split_line1[1]) - float(split_line2[1])) > 0.0001):
          print "NOT MATCHED \n";
          print line1;
          print line2;
          return -1;
      else:
        print "OFFSET NOT CORRECT\n";
        print split_line2[0];
        print split_line1[0];
        return -2;


  return offset

def main(masterFile, allFiles_arr):
  offset = 0;
  for i in range(0 , len(allFiles_arr)):
    print allFiles_arr[i]
    print offset
    offset = check_results(masterFile, allFiles_arr[i], offset)
    if(offset == -1):
      print "FAILED";
      print allFiles_arr[i];
      return -1;
    elif(offset == -2):
      print "\nOFFSET NOT CORRECT\n";
      print allFiles_arr[i];
      return -1;
  return 0;

if __name__ == "__main__":
  arg = sys.argv
  print arg;
  masterFile = arg[1];
  allFiles_arr = arg[2:]
  print allFiles_arr
  ret = main(masterFile, allFiles_arr)
  if(ret == 0):
    print "SUCCESS!!!!\n";
