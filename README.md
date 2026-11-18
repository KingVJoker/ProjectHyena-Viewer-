<p align="center">
  <img src="indra/newview/skins/default/textures/hyena_logo.png" alt="Project Hyena" width="180">
</p>

<h1 align="center">Project Hyena Viewer</h1>

<p align="center">
  A modern, different take on the Second Life viewer, built on Firestorm.<br>
  Fresh look, new ideas, and smooth performance from small older laptops to powerful new desktops.
</p>

<p align="center">
  <img alt="Status" src="https://img.shields.io/badge/status-0.1.0%20alpha-orange">
  <img alt="Platform" src="https://img.shields.io/badge/platform-Windows%20only-blue">
  <img alt="License" src="https://img.shields.io/badge/license-LGPL--2.1-green">
  <a href="https://github.com/KingVJoker/ProjectHyena-Viewer-/releases"><img alt="Release" src="https://img.shields.io/github/v/release/KingVJoker/ProjectHyena-Viewer-?include_prereleases"></a>
</p>

<p align="center">
  <a href="https://github.com/KingVJoker/ProjectHyena-Viewer-/releases"><b>Download</b></a> &nbsp;|&nbsp;
  <a href="https://github.com/KingVJoker/ProjectHyena-Viewer-/issues/new/choose"><b>Report a bug or share an idea</b></a> &nbsp;|&nbsp;
  <a href="#new-to-github-start-here"><b>New to GitHub?</b></a>
</p>

## Screenshots

<p align="center">
  <img src="https://i.gyazo.com/362e58eebe729c95e8981782dff5dbd1.jpg" alt="Project Hyena login screen" width="32%">
  <img src="" alt="Project Hyena multi-factor authentication screen" width="32%">
  <img src="docs/images/inworld.png" alt="Project Hyena in-world" width="32%">
</p>
<p align="center">
  <i>Login screen &nbsp;|&nbsp; Multi-factor authentication screen &nbsp;|&nbsp; In-world</i>
</p>

## What is Project Hyena?

Project Hyena is a custom viewer for **Second Life**, the online virtual world. A **viewer** is the program you use to enter Second Life. Linden Lab makes an official one, and community projects such as **Firestorm** make their own.

Project Hyena is built on top of Firestorm, so it keeps the features many people already rely on, but it gives you a different experience from the moment you open it:

- **A new launcher and login screen.** You start from a screen made for Project Hyena, with your saved username and password, a destination guide with pictures, and news, instead of the usual login box.
- **A modern, fresh look.** The Project Hyena theme is the default, so the viewer feels like its own program from the first launch.
- **A focus on running well on modest hardware.** The goal is a viewer that stays smooth on small, older laptops as well as powerful new desktops, so more people can enjoy Second Life without buying new equipment.
- **Beginner-friendly defaults.** Advanced options such as RLV and the developer and advanced menus are off until you choose to turn them on.
- **A fully separate install.** Project Hyena has its own settings, cache and installer. It will not touch your Firestorm or other viewers, and you can keep both installed.

It works with your normal Second Life account. You do not need a new account.

**Where it is today:** this is an early alpha (version 0.1.0). The core of it works and is being tested, but it is the first public build, so expect rough edges, and please tell us what you find.

## Features

- New launcher and login screen with saved username and password
- Multi-factor authentication support on the login screen
- Destination guide with pictures
- Project Hyena theme by default
- Own settings, cache and installer, separate from Firestorm
- Performance work aimed at smooth frame rates on small and older PCs
- Beginner-friendly defaults, with advanced features available when you want them
- Everything Firestorm offers underneath, since the core viewer is Firestorm 7.2.5

## Platform support

| Platform | Status |
|---|---|
| **Windows 10 / 11 (64-bit)** | Supported (alpha) |
| **macOS** | **No build yet** |
| **Linux** | **No build yet** |

Project Hyena is **Windows only for now**. There is no Mac or Linux version at this time.

## Download and install

1. Open the **[Releases page](https://github.com/KingVJoker/ProjectHyena-Viewer-/releases)**.
2. Open the newest release and scroll down to **Assets**.
3. Click the file ending in `_Setup.exe` to download it.
4. Run the file and follow the installer.

**Windows SmartScreen warning:** the installer is not code-signed yet, so Windows may show a blue "Windows protected your PC" window. This is expected for a new, unsigned program. Click **More info**, then **Run anyway**. The full source code is public in this repository, so you can see exactly what the program is made of.

**System requirements**

- Windows 10 or 11, 64-bit
- A graphics card or integrated graphics that supports OpenGL
- This build also runs on CPUs without AVX support

## New to GitHub? Start here

GitHub is a website where software projects keep their code, their downloads, and a place for people to report problems. This page is the front page of Project Hyena.

- **You do not need an account** to look at this page or to download the viewer.
- **You need a free GitHub account** only to report a bug or share an idea. You can sign up at github.com.

| I want to... | Click here |
|---|---|
| **Download the viewer** | [Releases page](https://github.com/KingVJoker/ProjectHyena-Viewer-/releases). Open the newest release, scroll to **Assets**, and click the file ending in `_Setup.exe`. |
| **Report a bug** | [Report a bug](https://github.com/KingVJoker/ProjectHyena-Viewer-/issues/new/choose). Pick **Bug report** and fill in the form. |
| **Share an idea** | [Share an idea](https://github.com/KingVJoker/ProjectHyena-Viewer-/issues/new/choose). Pick **Feature request** and tell us what you would like to see. |
| **See what others reported** | [Open issues](https://github.com/KingVJoker/ProjectHyena-Viewer-/issues) |

## Reporting a problem

A good bug report helps get it fixed faster. Please include:

- Your Project Hyena version (shown in Help > About, or in the installer file name)
- Your Windows version and graphics card
- What happened, and what you expected instead
- The steps to make it happen again
- Screenshots or the log file, if you have them. Logs are in `%APPDATA%\ProjectHyena_x64\logs`

**Never post your password or your multi-factor codes.**

## Questions you might have

New to Project Hyena? These are the things people usually wonder about first.

**Is it free?**
Yes. The source code is open and the viewer is free to download and use.

**Do I need a new Second Life account?**
No. You sign in with your normal Second Life account.

**Will it change my Firestorm settings?**
No. Project Hyena keeps its own settings and cache in its own folders. If it made any change to your Firestorm, please report the issue. 

**Is this an official Linden Lab or Firestorm viewer?**
No. It is an independent community project built on Firestorm. See the credits below.

**Is there a Mac or Linux version?**
Not yet. Right now it is Windows only.

**Why does Windows warn me when I install it?**
The installer is not code-signed yet. See the SmartScreen note under Download and install.

## Roadmap

- Automatic updates
- Opening `secondlife://` links directly in Project Hyena
- More cleanup of leftover Firestorm wording
- A faster build for newer CPUs
- Mac and Linux builds are not available yet and have no date
- **Many more features to come.** Project Hyena is just getting started, and your ideas help shape what comes next.

## Building from source

Project Hyena builds on Windows with Visual Studio and CMake, the same way as Firestorm. See the build instructions in the [Firestorm wiki](https://wiki.firestormviewer.org/fs_compiling_firestorm) and use the `hyena-base` branch of this repository.

## How this was made

I'm **KingVJoker (Mr. J)**, and Project Hyena is my project. I came up with the idea, I decide what the viewer should be and how it should look and feel, and I test every build myself before anything is released.

I didnt do it alone. I work alongside **Claude**, an AI assistant made by Anthropic. Together we go through the viewer step by step: Claude helps me fix problems, track down issues, and refine and improve everything the project needs, from the code and the installer to the documentation. It also helped me get everything ready to be released on GitHub.

The AI assists, and I make the decisions. The direction, the testing and i made the final call on what ships for the viewer.

None of this would exist without the people behind **Firestorm** and the **Second Life viewer** that it is built on. Thank you to all of them.

## Credits and license

Project Hyena is based on [Firestorm](https://www.firestormviewer.org) (phoenix-firestorm 7.2.5), which is based on the Second Life viewer by Linden Lab. Thank you to the Firestorm team and to Linden Lab.

This project is released under the GNU Lesser General Public License v2.1, the same license as the viewer it is built from. See [`LICENSE`](LICENSE). The source code is in this repository.

Project Hyena is not affiliated with, endorsed by, or supported by Linden Lab or the Firestorm project. Second Life is a trademark of Linden Research, Inc.
