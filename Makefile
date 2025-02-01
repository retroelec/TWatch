PORT := /dev/ttyACM0
FQBN := esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi,LoopCore=0,DebugLevel=info
SOURCEFILES=$(wildcard *.c)

ifndef WIFI_SSID
$(error WIFI_SSID is not set)
endif

ifndef WIFI_PASS
$(error WIFI_PASS is not set)
endif

default:	TWatch.ino $(SOURCEFILES)
	@podman run -it --rm -v .:/workspace/TWatch arduino-cli-twatch compile --libraries=/workspace/TTGO_TWatch_Library,/workspace/T-Watch-Deps --fqbn $(FQBN) --build-property "compiler.cpp.extra_flags=-DWIFI_SSID=\"${WIFI_SSID}\" -DWIFI_PASS=\"${WIFI_PASS}\"" --build-path build TWatch.ino

compile:	default

upload:
	arduino-cli upload -p $(PORT) --fqbn $(FQBN) -i build/TWatch.ino.bin

# create file $HOME/.minirc.dfl 
# content:
# pu addcarreturn    Yes
monitor:
	minicom -D $(PORT) -b 115200

format:
	@clang-format -i TWatch.ino

# see https://arduino.github.io/arduino-cli/1.0/getting-started/
install:	check_install
	podman build -t arduino-cli-twatch .

check_install:
	@echo -n "Are you sure? [y/N] " && read ans && [ $${ans:-N} = y ]

