#!/bin/bash


mkdir -p build

DUDES="mocc silo si ss2pl tictoc cicada ermia"

for dude in $DUDES
do
  pushd $dude
    mkdir -p build
    pushd build
      cmake ../
      make -j 16
      cp ${dude}.exe ../../build/
    popd
  popd
done


pushd build
  for dude in $DUDES
  do
    ./${dude}.exe | tee ${dude}.out
  done
popd
