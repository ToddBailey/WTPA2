# Makefile for WTPA2 sample packer
# TMB
# Tue Nov 18 18:30:16 EST 2014

TARGET = wtpaAudioSamplePacker

all: $(TARGET)

wtpaAudioSamplePacker: $(TARGET).c
	$(CC) $(TARGET).c -o $(TARGET)

clean:
	rm -f $(TARGET)

install:
	cp $(TARGET) /usr/local/bin
