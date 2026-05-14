#
# Makefile for CardputerZero early splash (Circle bare metal)
#
# Build: ./configure -r 3 -p aarch64-none-elf- in circle/, then make here
#

CIRCLEHOME = ../..

OBJS = main.o kernel.o py32io16.o

LIBS = $(CIRCLEHOME)/addon/SDCard/libsdcard.a \
       $(CIRCLEHOME)/addon/fatfs/libfatfs.a \
       $(CIRCLEHOME)/lib/usb/libusb.a \
       $(CIRCLEHOME)/lib/input/libinput.a \
       $(CIRCLEHOME)/lib/fs/libfs.a \
       $(CIRCLEHOME)/lib/libcircle.a

include $(CIRCLEHOME)/Rules.mk

-include $(DEPS)
