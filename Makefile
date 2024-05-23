
vpath %.h h
vpath %.cpp src


DIR 	?= deliverables
EXE 	:= $(DIR)/procstat
CXXFLAGS 	:= 	-Ih -std=c++11 -Wall
SOURCE 	:= 	$(notdir $(wildcard src/*.cpp))
OBJS 	:=	$(SOURCE:.cpp=.o)
DEP 	=	$(OBJS:.o=.d)
debug: CXXFLAGS += -g -DDEBUG

.PHONY: 	clean all debug

#-include $(DEP)

all:	|$(DIR) $(EXE)

debug: 	all

$(EXE):	 $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^
	@echo "Built as: $@"

$(DIR):
	mkdir $@


clean:
	rm -f  $(OBJS) $(DEP) $(EXE)


#%.d: %.cpp
#	@echo "Dependencies reconstructed for $*"
#	@set -e; rm -f $@; \
#	$(CXX) -M $(CXXFLAGS) $< > $@.$$$$; \
#	sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
#	rm -f $@.$$$$
