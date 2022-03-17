fix-cec-serial: main.cc
	g++ -Wall -I /opt/vc/include -L /opt/vc/lib -l bcm_host -l vchiq_arm -l vcos main.cc -o fix-cec-serial

clean:
	rm fix-cec-serial

install: fix-cec-serial
	cp fix-cec-serial /usr/local/bin
	cp fix-cec-serial.service /etc/systemd/system/
	systemctl enable fix-cec-serial
