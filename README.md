# **GRP SAE : B1.2 - Tole Tole**

# **Aide a la compilation**
ne connaisant pas l'environnement dans lequel vous aller compiler les programmes il se pourrait que vous ayez quelques erreur de compilation.

l'aide a la compilation qui est fourni correspond a cette OS : **Ubuntu sous WSL**

### **1. Installation des librairies**

la premiere etape est d'installer la librairie : **`libpq`**

### **2. Lancement de la BDD**

le bon fonctionnement du programme nescessite une BDD local

2 script sont present dans le dossier BDD : 

* `cr_bdd_sprint1_postgresql.sql`
* `pop_bdd_sprint1_postgresql.sql`

vous pouvez les executer en local puis completer les données nescessaires sur le fichier serveur.c a la ligne **1377** :

```
const char *conninfo = " host=localhost port=5432 dbname=... user=... password=... ";
```

### **3. Compiler**

vous devez utiliser le fichier Makefile qui permet de compiler tout les fichier directement avec cette commande : `make`

Cependant comme expliqué plus tot, il est possible que vous ayez des erreurs liée a la configuration du makefile et des chemin d'accées au librairie. 

Pour cela editer le debut du fichier Makefile avec vos bon chemin de fichier.

# **Information Complementaire**

### **Login**
toute les infos des utilisateurs sont retrouvable dans la bdd : `pop_bdd_sprint1_postgresql.sql`.

* Admin 
    * id : 1 
    * cle Api :rwda-b894e4c2-6eee-4b9c-b01e-456b8b45e28b
* Pro
    * John 
        * id : 2
        * cle Api :rwd-b894e4c2-6eee-4b9c-b01e-456b8b45e28b
    * Lou 
        * id : 3 
        * cle Api :rwd-89af9b99-ad08-4a93-b8c9-d8398c18c016
* Membre
    * Piel
        * id : 6
        * cle Api :rw-6526e1ae-a426-42f7-9f24-96735b2b5085 
    * Jane
        * id : 7
        * cle Api :rw-d07777ac-304d-4ba8-b571-cdb6dd5bc71d 

### **Documentation**

des informations complementaire sur le client et le serveur sont presente dans le dossier

### **Log**

le fichier de log `tchatator.log` ce genere automatiquement au lancement du serveur


# **Execution**

une fois que tout est compilé sans erreur vous pouvez lancer le serveur en ligne de commande : `./serveur` 

puis le client sur un autre terminal : `./client` 

