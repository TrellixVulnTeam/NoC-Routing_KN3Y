import os
i = 0
print("Starting Training Phase")
os.system('scons ./build/X86/gem5.opt')
while(i<10):
    os.system('./build/X86/gem5.opt -d m5out/ configs/example/garnet_synth_traffic.py --num-cpus=16 --num-dirs=16 --network=garnet --topology=Mesh_XY --mesh-rows=4 --sim-cycles=100000 --inj-vnet=-1 --vcs-per-vnet=8 --injectionrate=0.1 --synthetic=uniform_random   --routing-algorithm=3')

    i += 1

print("Training Phase Ended")
