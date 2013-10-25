# Makefile for WTPA2 boot image packer
# TMB
# Tue Oct  2 10:13:07 EDT 2012

TARGET = wtpaBootImagePacker

all: $(TARGET)

wtpaBootImagePacker: $(TARGET).c
	$(CC) $(TARGET).c -o $(TARGET)

clean:
	rm -f $(TARGET)

install:
	cp $(TARGET) /usr/local/bin
