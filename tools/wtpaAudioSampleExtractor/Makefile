# Makefile for WTPA2 sample extractor
# TMB
# Tue Nov 11 18:18:02 EST 2014

TARGET = wtpaAudioSampleExtractor

all: $(TARGET)

wtpaAudioSampleExtractor: $(TARGET).c
	$(CC) $(TARGET).c -o $(TARGET)

clean:
	rm -f $(TARGET)

install:
	cp $(TARGET) /usr/local/bin
