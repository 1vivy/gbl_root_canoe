#run all tests
cd "$(dirname "$0")"
./001_test_extract.sh
if [ $? -ne 0 ]; then
    echo "Test 001_test_extract.sh failed. Stopping further tests."
    exit 1
fi
./002_machine_specific_build.sh
if [ $? -ne 0 ]; then
    echo "Test 002_machine_specific_build.sh failed. Stopping further tests."
    exit 1
fi
./003_test_build_generic.sh
if [ $? -ne 0 ]; then
    echo "Test 003_test_build_generic.sh failed. Stopping further tests."
    exit 1
fi
./004_test_patch.sh
if [ $? -ne 0 ]; then
    echo "Test 004_test_patch.sh failed. Stopping further tests."
    exit 1
fi
./005_test_hooks_build.sh
if [ $? -ne 0 ]; then
    echo "Test 005_test_hooks_build.sh failed. Stopping further tests."
    exit 1
fi
echo "All tests passed successfully."