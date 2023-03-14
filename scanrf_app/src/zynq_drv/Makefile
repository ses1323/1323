OBJ = i2c_ctrl.o

all:$(OBJ)
$(OBJ):%.o:%.cpp #添加生成依赖规则，这个很重要
	$(CC) -c $^ $(INC) -static -g -O2 -o $(OBJDIR)/$@    #添加obj路径

ECHO:
	@echo $(OBJS)
clean:
	-rm $(OBJ)
