# ccminer

Based on Christian Buchner's &amp; more recently Chris monkins1010.

Check the [README.txt](README.txt) for the additions

BTC donation address: 34KxubUp46B3tcWfUp58R3M9Uj6Hkqda1R (sfitzjava)

A part of the recent algos were originally written by [djm34](https://github.com/djm34) and [alexis78](https://github.com/alexis78)

This variant was tested and built on Raspberry Pi zero 2 w and Pi 3 a+ with raspios_lite_arm64

Note: This is a CPU only based build and does not support any CUDA access or GPU accelerated mining.

With a 4 core CPU running 3 thread performance was see to be around 80kh, but ranging from 30 to 100kh.


About source code dependencies
------------------------------
Initially use the following command to install needed libraries
sudo apt install git libcurl4-openssl-dev libssl-dev libjansson-dev automake autotools-dev build-essential

Then you can run the build.sh and there will be some warnings but should not be any errors.
Upon completion of the compile there should be an executable ccminer in the directory for use on your Raspberry pi.
