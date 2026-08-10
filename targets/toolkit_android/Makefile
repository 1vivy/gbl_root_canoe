clean:
	rm -rf build || true
copy_resources:
	mkdir -p build/toolkit
	mkdir -p build/toolkit/bin
	cp -r resources/* build/toolkit/
submodule_uefi: copy_resources
	cd ../../submodules/uefi && make build
	cp ../../submodules/uefi/build/BDS.efi ./build/toolkit/BDS.efi
submodule_patcher: copy_resources
	cd ../../submodules/patcher && make build_android
	cp ../../submodules/patcher/build/patch_abl_android ./build/toolkit/bin/patch_abl
submodule_ablfvextractor: copy_resources
	cd ../../submodules/ablfvextractor && make build_android
	cp ../../submodules/ablfvextractor/build/extractfv_android ./build/toolkit/bin/extractfv
build: submodule_uefi submodule_patcher submodule_ablfvextractor
	# zip
	cd build/toolkit && zip -r ../toolkit_android.zip ./
