#!/usr/bin/python3
import sys, pcbnew, re, collections
b = pcbnew.LoadBoard(sys.argv[1])
ok = pcbnew.WriteDRCReport(b, sys.argv[2], pcbnew.EDA_UNITS_MILLIMETRES, True)
txt = open(sys.argv[2]).read()
print(ok, re.findall(r"\*\* Found \d+ [^*]+", txt))
c = collections.Counter(re.findall(r"^\[(\w+)\]", txt, re.M)); print(dict(c))
