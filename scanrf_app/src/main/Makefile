OBJ = main.o
OBJ += thread.o
OBJ += fpga_ctl.o
OBJ += wifi_config.o
OBJ += debug_func.o
OBJ += temp_ctl.o
OBJ += gpio_ctl.o
OBJ += int.o
OBJ += vi_update.o
OBJ += log_manager.o
all:$(OBJ)
$(OBJ):%.o:%.cpp #添加生成依赖规则，这个很重要
	$(CC) -c $^ $(INC) -g -O2 -static -o $(OBJDIR)/$@    #添加obj路径

ECHO:
	@echo $(OBJS)
clean:
	-rm $(OBJ)
