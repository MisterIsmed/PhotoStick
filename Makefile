-include config.mk

all: Photostick.cpp
	$(ARDUINO) --verify $<

# -Wl,-Map=output.map
debug: Photostick.cpp
	$(ARDUINO) --verbose-build --preserve-temp-files --verify $<

upload: Photostick.cpp
	$(ARDUINO) --upload $< --port $(PORT)
