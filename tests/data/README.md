# Test device trees

`rk3568-anbernic-rg-ds.dtb` and `rk3568-anbernic-rg-ds-plus.dtb`: ROCKNIX's RG DS and RG DS Plus device trees
(`projects/ROCKNIX/devices/RK3566/linux/dts/rockchip/` at ROCKNIX/distribution 2d34256), on mainline 7.1.2's
`rk3568.dtsi` (03e2778), built with `cpp -nostdinc -undef -D__DTS__ -x assembler-with-cpp` and `dtc -I dts -O dtb`.
The sources are `(GPL-2.0+ OR MIT)`. `tests/test_undervolt.py` patches their OPP tables.
