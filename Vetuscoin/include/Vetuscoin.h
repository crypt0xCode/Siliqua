#pragma once
#include "crypto/ecdsa.h"
#include "core/transaction.h"
#include "core/block.h"
#include "consensus/pow.h"
#include "storage/storage.h"
#include "network/node.h"
#include "network/daemon.h"
#include "logger.h"
#include "utils.h"

#include <picosha2.h>
#include <print>
#include <iostream>
#include <ctime>
#include <map>