# Copyright Vladislav Aleinik, 2025

#-------
# Files
#-------

# By default, build executable:
# NOTE: first target in the file is the default.

ifeq ($(CORES),)
	CORES=8
endif

default: library test

#-----------------------
# Compiler/linker flags
#-----------------------

CC := gcc


# Compiler flags:
CFLAGS = \
	-std=c2x \
	-Wall    \
	-Wextra  \
	-Werror  \
	-fsanitize=address,leak

# Linker flags:
LDFLAGS = -pthread -lrt -lm
CLIBFLAGS = -fpic -shared 
# Select build mode:
# NOTE: invoke with "DEBUG=1 make" or "make DEBUG=1".
ifeq ($(DEBUG),1)
	# Add default symbols:
	CFLAGS += -g -DDEBUGTEST -fsanitize=address,leak,undefined
	CLIBFLAGS += -g -DDEBUGTEST -fsanitize=address,leak,undefined
else
	# Enable link-time optimization:
	CFLAGS  += -flto
	LDFLAGS += -flto
endif

#--------
# Colors
#--------

# Use ANSI color codes:
BRED    = \033[1;31m
BGREEN  = \033[1;32m
BYELLOW = \033[1;33m
GREEN   = \033[1;35m
BCYAN   = \033[1;36m
RESET   = \033[0m

#-------------------
# Build/run process
#-------------------

test : test_manager test_worker
	@echo "Запуск тестов..."
	@./build/test_manager 127.0.0.1 1227 1 1 2>/dev/null &
	@time ./build/test_worker 127.0.0.1 1227 $(CORES)
	@echo "Тесты завершены!"

library: worker manager

manager: manager.c
	@printf "$(BYELLOW)Building library $(BCYAN)$<$(RESET)\n"
	@mkdir -p libs
	$(CC) $(CLIBFLAGS) $(CFLAGS) $< -o libs/libmanager.so $(LDFLAGS)
	@sudo cp libs/libmanager.so /usr/local/lib
	@sudo ldconfig
	@printf "$(BYELLOW)Library $(BCYAN)libmanager$(BYELLOW) installed to /usr/local/lib$(RESET)\n"

test_manager: test_manager.c
	@printf "$(BYELLOW)test_manager$(BCYAN)$<$(RESET)\n"
	@mkdir -p build
	$(CC) $(CFLAGS) $< -o build/$@ $(LDFLAGS) -lmanager

worker: worker.c
	@printf "$(BYELLOW)Building library $(BCYAN)$<$(RESET)\n"
	@mkdir -p libs
	$(CC) $(CLIBFLAGS) $(CFLAGS) $< -o libs/libworker.so $(LDFLAGS)
	@sudo cp libs/libworker.so /usr/local/lib
	@sudo ldconfig
	@printf "$(BYELLOW)Library $(BCYAN)libworker$(BYELLOW) installed to /usr/local/lib$(RESET)\n"

test_worker: test_worker.c
	@printf "$(BYELLOW)test_worker$(BCYAN)$<$(RESET)\n"
	@mkdir -p build
	$(CC) $(CFLAGS) $< -o build/$@ $(LDFLAGS) -lworker

clean:
	@printf "$(BYELLOW)Cleaning build directory$(RESET)\n"
	@rm -rf build
	@rm -rf libs
full-clean: clean
	@sudo rm /usr/local/lib/libmanager.so
	@sudo rm /usr/local/lib/libworker.so
