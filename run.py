#!/usr/bin/env python

import os, sys, subprocess

def main():
  t_sum = 0;
  for i in xrange(0,100):
    t = int(subprocess.check_output('./test', shell = True))
    t_sum += t
  print t_sum

if __name__ == '__main__':
  main()
