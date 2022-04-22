make-gpt: gpt.cpp
	g++ -std=c++23 -Wall -Werror -Wextra -Wpedantic gpt.cpp -o make-gpt

gpt.bin: make-gpt gpt.conf
	cat gpt.conf | ./make-gpt

parted: gpt.bin
	parted ./gpt.bin -- p
