#OBJ = udpclient.o
#OBJ += udpserver.o
OBJ += tcpserver.o
#OBJ += tcpclient.o
#OBJ += tftp_client.o
OBJ += ev_demo.o
OBJ += ccl.o
all:$(OBJ)
$(OBJ):%.o:%.cpp #添加生成依赖规则，这个很重要
	$(CC) -c $^ $(INC) -g -O2 -o $(OBJDIR)/$@    #添加obj路径

ECHO:
	@echo $(OBJS)
clean:
	-rm $(OBJ)
