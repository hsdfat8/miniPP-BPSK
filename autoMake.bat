cd modem
make
del *.o
cd..


cd vad
make
del *.o
cd ..

make
cd modem
del *.a
cd ..
cd vad
del *.a
cd ..
del *.o
