FESTIVAL SING MODE - SOURCE PORTABLE BUNDLE
===========================================

Questo archivio contiene il minimo necessario per:

1. ricompilare offline il frontend Win32;
2. eseguire il programma con il runtime Festival locale.

REQUISITO ESTERNO
-----------------
Visual Studio 2013 oppure Build Tools compatibili con il toolset v120,
con il compilatore Visual C++ x86.

BUILD
-----
Aprire un Developer Command Prompt e lanciare:

    build-release-win32.cmd

CONTENUTO
---------
- sorgenti C++ del frontend;
- soluzione e progetto Visual Studio;
- wxWidgets: soli header e librerie statiche vc_lib;
- FestivalTTSCOM.dll Win32;
- festival/lib con script Scheme, lessici e dati voce;
- nessun sorgente completo wxWidgets;
- nessun obj_debug/obj_release;
- nessun database .sdf/.suo;
- nessun archivio tar.gz duplicato.

CONFINE DI RICOMPILABILITA'
---------------------------
Il frontend è ricompilabile integralmente.

FestivalTTSCOM.dll e le librerie statiche wxWidgets sono dipendenze binarie
incluse nello ZIP. Per ricompilare anche queste dipendenze servirebbe un
"full dependency source bundle" separato e sensibilmente più grande.
