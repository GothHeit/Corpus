#ifndef OPEN_WITH_HPP
#define OPEN_WITH_HPP

// Corpus in the "Open with" list of Windows, for the kinds of files it shows. Everything goes under
// HKEY_CURRENT_USER\Software\Classes, so it needs no administrator rights and leaves the default program of every
// type as it is. Whether it is on is whether the entries exist: nothing else is kept.
namespace open_with
{
    // Whether Corpus is in the list.
    bool IsOn();
    // Adds Corpus to the list for each type, opening files with this .exe. False if the registry refused.
    bool TurnOn();
    // Takes it out again, leaving what the user or other programs put in the same keys.
    void TurnOff();
    // When it is on for another .exe than this one (Corpus was moved or updated), points it here.
    void Refresh();
}

#endif
