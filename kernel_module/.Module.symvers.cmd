cmd_/root/Memory-Fragmentation-Tool/kernel_module/Module.symvers := sed 's/\.ko$$/\.o/' /root/Memory-Fragmentation-Tool/kernel_module/modules.order | scripts/mod/modpost -m -a  -o /root/Memory-Fragmentation-Tool/kernel_module/Module.symvers -e -i Module.symvers   -T -
