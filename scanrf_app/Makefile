CC=aarch64-linux-gnu-g++
#CC=arm-linux-gnueabihf-g++
TAG = scanrf_app

TOPDIR = $(PWD)
OBJDIR = $(TOPDIR)/obj
BINDIR = $(TOPDIR)/bin
SRCDIR = $(TOPDIR)/src
LIB = -L$(TOPDIR)/lib
INC = -I$(TOPDIR)/inc

VERSION_STRING := "V1.1.6"
DATE_STRING := `date "+20%y.%m.%d %k:%M"`
GITVER_STRING := $(shell git show -s --pretty=format:%h)

export CC TAG TOPDIR SUBDIR OBJDIR BINDIR INC LIB

.PHONY:all
all:CHECK $(SRCDIR) $(TAG)

CHECK:
	mkdir -p $(OBJDIR) $(BINDIR)
	`touch $(TOPDIR)/inc/auto_version.h`
	@echo "#define VER_AUTO 1" > $(TOPDIR)/inc/auto_version.h                         # > :覆盖文本原来内容
	@echo "#define VERSION \"$(VERSION_STRING)\"" >> $(TOPDIR)/inc/auto_version.h     # >> :追加内容到文本末尾
	@echo "#define DATE \"$(DATE_STRING)\""	>>$(TOPDIR)/inc/auto_version.h           # >> :追加内容到文本末尾
	@echo "#define GITVER \"$(GITVER_STRING)\""	>>$(TOPDIR)/inc/auto_version.h           # >> :追加内容到文本末尾

$(SRCDIR):ECHO
	make -C $@

$(TAG):ECHO        #这里只需要把obj路径下的.o文件编译加载就行
	$(CC) -o $(BINDIR)/$(TAG)  $(wildcard $(OBJDIR)/*.o) -lpthread  $(LIB) -lev -liniparser
#   	$(CC) -o $(addprefix $(BINDIR)/,$(TAG)) $(wildcard $(OBJDIR)/*.o)


ECHO:
	@echo $@


.PHONY : clean
clean :
	-rm $(BINDIR)/$(TAG)
	-rm $(TOPDIR)/inc/auto_version.h
	-rm $(OBJDIR)/*.o  #这里的clean就不需要去调用各层的Makefile来执行，更方便清理
