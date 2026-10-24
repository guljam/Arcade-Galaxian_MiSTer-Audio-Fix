---------------------------------------------------------------------------------
--
-- Arcade-Galaxian_MiSTer-Audio-Fix
-- Galaxian core for MiSTer with a rewritten sound section.
-- Based on MiSTer-devel/Arcade-Galaxian_MiSTer (original credits below).
--
-- OSD "Sound" option (can be switched during play):
--   Analog Circuit : default. Model of the analog sound circuit after MAME's
--                    discrete netlist (MAME 0.133u2 and later): 555 timers,
--                    RC networks, op-amp filter and mixer with the real part
--                    values.
--   Legacy Synth   : the older synthesized sound of MAME 0.36b11 - 0.133u1,
--                    as still used by FBNeo (softer timbre). The swarm hum is
--                    raised to near the Analog Circuit level (FBNeo plays it
--                    about 17 dB quieter), so switching mainly changes the
--                    timbre, not the balance.
--
-- Changes from the original core:
--   * New sound blocks: rtl/galaxian_snd.vhd (Analog Circuit) and
--     rtl/galaxian_snd_fbn.vhd (Legacy Synth). They replace the old
--     mc_sound_* blocks and the fire/explosion sample ROMs.
--   * Swarm hum: pulse waves instead of a sine-like tone, and the rhythm
--     follows the non-linear LFO of the circuit (it speeds up correctly as
--     a stage goes on).
--   * Applies to every game of this core that uses the Galaxian sound
--     hardware; King & Balloon speech is unchanged.
--   * Cause, measurements and verification: docs/fixes-2026-10-22.md,
--     docs/fixes-2026-10-23.md, docs/fixes-2026-10-24.md
--     (the simulation tools they mention are not part of this repository).
--
-- Release: releases/Arcade-Galaxian_20261024.rbf
--
---------------------------------------------------------------------------------
-- 
-- Arcade: Galaxian port to MiSTer by Sorgelig
-- 15 November 2017
-- 
---------------------------------------------------------------------------------
-- A simulation model of Galaxian hardware
-- Copyright(c) 2004 Katsumi Degawa
---------------------------------------------------------------------------------
-- 
-- Support screen and controls rotation on HDMI output.
-- Only controls are rotated on VGA output.
-- 
-- 
-- Keyboard inputs :
--
--   F2         : Coin + Start 2 players
--   F1         : Coin + Start 1 player
--   SPACE,CTRL : Fire
--   LEFT,RIGHT : Movements
--
-- MAME/IPAC/JPAC Style Keyboard inputs:
--   5           : Coin 1
--   6           : Coin 2
--   1           : Start 1 Player
--   2           : Start 2 Players
--   D,G         : Player 2 Movements
--   A           : Player 2 Fire
--
--
-- Joystick support.
-- 
---------------------------------------------------------------------------------

                                *** Attention ***

ROMs are not included. In order to use this arcade, you need to provide the
correct ROMs.

To simplify the process .mra files are provided in the releases folder, that
specifies the required ROMs with checksums. The ROMs .zip filename refers to the
corresponding file of the M.A.M.E. project.

Please refer to https://github.com/MiSTer-devel/Main_MiSTer/wiki/Arcade-Roms for
information on how to setup and use the environment.

Quickreference for folders and file placement:

/_Arcades/<game name>.mra
/_Arcades/cores/<game rbf>.rbf
/_Arcades/mame/<mame rom>.zip
/_Arcades/hbmame/<hbmame rom>.zip
