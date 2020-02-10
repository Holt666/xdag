find_path(OPENSSL_ROOT_DIR
        NAMES include/openssl/crypto.h
        PATHS /usr/local/opt/ /opt/local/ /usr/local/ /usr/
        NO_DEFAULT_PATH
        )

find_path(OPENSSL_INCLUDE_DIR
        NAMES openssl/crypto.h
        PATHS ${OPENSSL_ROOT_DIR}/include
        NO_DEFAULT_PATH
        )

find_library(OPENSSL_SSL_LIBRARY
        NAMES ssl
        PATHS ${OPENSSL_ROOT_DIR}/lib
        NO_DEFAULT_PATH
        )

find_library(OPENSSL_CRYPTO_LIBRARY
        NAMES crypto
        PATHS ${OPENSSL_ROOT_DIR}/lib
        NO_DEFAULT_PATH
        )

if(OPENSSL_INCLUDE_DIR AND OPENSSL_SSL_LIBRARY AND OPENSSL_CRYPTO_LIBRARY)
    set(OPENSSL_FOUND TRUE)
else()
    set(OPENSSL_FOUND FALSE)
endif()

#set(OPENSSL_LIBRARY ${OPENSSL_SSL_LIBRARY} ${OPENSSL_CRYPTO_LIBRARY} )

mark_as_advanced(
        OPENSSL_ROOT_DIR
        OPENSSL_INCLUDE_DIR
        OPENSSL_LIBRARY
)

set(CMAKE_REQUIRED_INCLUDES ${OPENSSL_INCLUDE_DIR})
set(CMAKE_REQUIRED_LIBRARIES ${OPENSSL_SSL_LIBRARY})
set(CMAKE_REQUIRED_LIBRARIES ${OPENSSL_CRYPTO_LIBRARY})