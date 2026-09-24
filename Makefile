CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -g

RM_SRC = \
	rm/rm_test.cc \
	rm/rm_manager.cc \
	rm/rm_filehandle.cc \
	rm/rm_filescan.cc \
	rm/rm_record.cc \
	rm/rm_rid.cc \
	rm/rm_error.cc

PF_SRC = \
	pf/pf_manager.cc \
	pf/pf_filehandle.cc \
	pf/pf_pagehandle.cc \
	pf/pf_buffermgr.cc \
	pf/pf_hashtable.cc

RM_OBJ = $(RM_SRC:.cc=.o)
PF_OBJ = $(PF_SRC:.cc=.o)

OBJS = $(RM_OBJ) $(PF_OBJ)

TARGET = rm_test


$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(TARGET)


%.o: %.cc
	$(CXX) $(CXXFLAGS) -c $< -o $@


clean:
	rm -f $(OBJS) $(TARGET)


.PHONY: clean