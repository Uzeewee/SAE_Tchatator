## **Aide a la compilation**
ne connaisant pas l'environnement dans lequel vous aller compiler les programmes il se pourrait que vous ayez quelques erreur de compilation.

l'aide a la compilation qui est fourni correspond a cette OS : **Ubuntu sous WSL**

### **1. Installation des librairies**

la premiere etape est d'installer **libpq**

### **2. lancement de la BDD**

le bon fonctionnement du programme nescessite une BDD local

2 script sont present dans le dossier BDD : 

* `cr_bdd_sprint1_postgresql.sql`
* `pop_bdd_sprint1_postgresql.sql`

### **3. compiler**

vous pouvez utiliser le fichier Makefile qui permet de compiler tout les fichier directement avec cette commande :

`make`

Cependant comme expliqué plus tot, il est possible que vous ayez des erreurs liée a la configuration du makefile et des chemin d'accées au librairie. 

Pour cela editer le fichier Makefile ou lancer directement votre compilation en ligne de commande en changeant les :

```
gcc -o serveur serveur.c -I/usr/include/postgresql -L/usr/lib/postgresql -lpq

gcc -o client client.c

gcc -o generate_config generate_config.c

```

