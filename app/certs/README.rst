Root certificates
#################

The certificate authorities that the player trusts, in DER form. Each is the
root that a server the player talks to chains up to:

.. list-table::
   :header-rows: 1

   * - File
     - Root
     - Servers
   * - ``digicert_global_root_g2.der``
     - DigiCert Global Root G2
     - ``*.spotify.com``: access point look-up, login5, Web API
   * - ``starfield_root_g2.der``
     - Starfield Root Certificate Authority - G2
     - ``accounts.spotify.com``: Web API tokens
   * - ``digicert_global_root_g3.der``
     - DigiCert Global Root G3
     - ``i.scdn.co`` (covers), ``*.spotifycdn.com`` (audio)
   * - ``globalsign_root_r3.der``
     - GlobalSign Root CA - R3
     - ``*.scdn.co`` on the other CDN (audio, covers)
   * - ``gts_root_r4.der``
     - GTS Root R4
     - ``lrclib.net``: lyrics

They were taken from the trust store of a Linux distribution. When a server
moves to another authority, connections to it fail until its root is added
here, in ``app/src/certs.c`` and to ``CONFIG_ZSPOT_TLS_SEC_TAG_COUNT``. To
see what a server presents:

.. code-block:: sh

   openssl s_client -connect <host>:443 -servername <host> </dev/null | \
       sed -n '/Certificate chain/,/---/p'
