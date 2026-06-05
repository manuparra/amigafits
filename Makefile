.PHONY: build clean run-fsuae

build:
	./scripts/build.sh

run-fsuae:
	./scripts/run-fsuae.sh

clean:
	rm -rf build dist
