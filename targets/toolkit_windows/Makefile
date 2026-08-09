clean:
	rm -rf build || true
copy_resources:
	mkdir -p build/toolkit
	mkdir -p build/toolkit/bin
	cp -r resources/* build/toolkit/
submodule_uefi: copy_resources
	cd ../../submodules/uefi && make build
	cp ../../submodules/uefi/build/BDS.efi ./build/toolkit/BDS.efi
build: submodule_uefi
	# zip
	cd build/toolkit && zip -r ../toolkit_windows.zip ./
