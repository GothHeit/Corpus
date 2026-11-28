#ifndef SAVING_HPP
#define SAVING_HPP

#include "../include/library.hpp"

class library;
class database;



/// Fills the library from the database (making the tables the first time) and attaches it.
bool open_library(library &lib, database &db);


#endif