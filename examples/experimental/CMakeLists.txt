ADD_EXECUTABLE(stock_loan stock_loan.cpp)
ADD_EXECUTABLE(glwb glwb.cpp)

TARGET_LINK_LIBRARIES(stock_loan ${JSONCPP_LIBRARY})
TARGET_LINK_LIBRARIES(glwb ${JSONCPP_LIBRARY})
