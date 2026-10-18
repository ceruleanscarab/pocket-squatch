package com.ceruleanscarab.pocketsquatch;

import org.libsdl.app.SDLActivity;

// SDL runs the game (app/src/main/cpp/main.cpp, built as libmain.so); this only names it.
public class MainActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL2", "main"};
    }
}
