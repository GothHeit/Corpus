#include "../include/saving.hpp"
#include "../include/database.hpp"
#include "../include/tag.hpp"
#include "../include/file.hpp"
#include "../include/library.hpp"

/// Fills an empty library from an open database and attaches it. The first time (no tables yet) it creates them in
/// one transaction, so a crash halfway leaves the database new. On failure the library is left empty and detached,
/// so open_library can simply be called again. See database::last_error.
bool open_library(library &lib, database &db)
{
    lib.attach(nullptr);
    if(!db.is_new())
    {
        if(!db.load(lib))
        {
            lib.clear();
            return false;
        }
        lib.attach(&db);
        return true;
    }

    if(!db.begin())
    {
        return false;
    }
    db.clear_error();
    if(!db.create_schema())
    {
        db.rollback();
        return false;
    }
    lib.attach(&db);
    if(!db.last_error().empty() || !db.commit())
    {
        db.rollback();
        lib.attach(nullptr);
        lib.clear();
        return false;
    }
    return true;
}