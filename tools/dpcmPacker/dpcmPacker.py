# DPCM Packer / Andrew Reitano 10/2013
# http://www.batslyadams.com
# Python is kinda cool but where the datatypes at?

# Fixed some typos, changed some user feedback -- Fri Nov 21 17:10:08 EST 2014
# TMB

# TODO:
# * I/O Arguments

import glob		# For enumerating files in a directory
import os 		# File functions, get size etc..
import sys		# Exit calls

print ""
print "DPCM Packer v0.1 - Batsly Adams 10/2013"

if (len(sys.argv) != 3)	 :
	print "This tool gathers all files regardless of extension in the passed directory and arranges a bank usable by the DPCM code."
	print "Each bank can hold up to 128 4KB samples."
	print "Usage : dpcmPacker.py <sample directory> <output file>"
	sys.exit(1)
print ""

fileList = glob.glob(sys.argv[1] + "/*.*")

# Dummy checks...
print "Files found: (" + str(len(fileList)) + ")"

if (len(fileList) > 128) :
	print "Oops. Too many files. (" + str(len(fileList)) + ")"
	sys.exit(1)

print "Checking sample sizes..."
for i in range(0, len(fileList)-1) :
	#print fileList[i]
	if (os.path.getsize(fileList[i]) > 4095) :
		print "Whoops. " + fileList[i] + " is greater than 4KB."
		sys.exit(1)

sdImage = open(sys.argv[2], 'wb')

# FIXME - double check specs with Todd
# Yr Good. -- TMB

print "Writing header..."
sdImage.write("WTPA")			# 4 chars "WTPA"
sdImage.write("DPCM")			# Extended descriptor ("SAMP", "BOOT", "DPCM" -- indicates the type of data on the card)
#sdImage.write("DONTCARE")		# Don't cares in the DPCM case!
#sdImage.write("SWAGRARE")
for i in range(0, 512-8) :
	sdImage.write(chr(0x00));	# Pad out to 512b	

print "Building image..."
for i in range(0, len(fileList)) :
	currentSample = open(fileList[i], 'rb')
	sdImage.write(currentSample.read())
	for j in range(0, (4096-os.path.getsize(fileList[i]))) :	# Pad out the rest with idle waveforms (0xAA)
		sdImage.write(chr(0xAA));

# Uh oh - how the hell do I handle the datatypes here.. Python is all like "wats a bit"
# EDIT : Ok it's not so bad - used a 32bit int to represent these for some reason in the player? I think it was so it would be a nice even 512b chunk. I'll stick with it for now.
print "Writing length table..."
for i in range(0, len(fileList)) :
	lengthCalc = os.path.getsize(fileList[i])
	sdImage.write(chr(lengthCalc & 0xFF))
	sdImage.write(chr((lengthCalc>>8) & 0xFF))
	sdImage.write(chr((lengthCalc>>16) & 0xFF))
	sdImage.write(chr((lengthCalc>>24) & 0xFF))

print "Done!"
print ""
