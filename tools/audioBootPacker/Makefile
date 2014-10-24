# Makefile for WTPA2 boot image packer
# Audio Version
# TMB
# Fri Oct 24 15:53:00 EDT 2014

TARGET = wtpaAudioBootImagePacker

all: $(TARGET)

wtpaAudioBootImagePacker: $(TARGET).c
	$(CC) $(TARGET).c -o $(TARGET)

clean:
	rm -f $(TARGET)

install:
	cp $(TARGET) /usr/local/bin
