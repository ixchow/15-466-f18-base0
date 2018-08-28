.PHONY : all

HOSTNAME := $(shell hostname)

ifeq ($(HOSTNAME), incepchow)
	BLENDER = ~/blender-2.79b-linux-glibc219-x86_64/blender
else
	BLENDER = blender
endif

DIST=../dist

all : \
	$(DIST)/meshes.blob \


$(DIST)/meshes.blob : meshes.blend export-meshes.py
	$(BLENDER) --background --python export-meshes.py -- '$<' '$@'
