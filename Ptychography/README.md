
# Ptychography

![3D](https://github.com/Ryota-Koda/Tech-Profile/blob/main/Ptychography/3D_image.png "3D")  
![2D](https://github.com/Ryota-Koda/Tech-Profile/blob/main/Ptychography/2D_image.png "2D")  

ePIE(Extended Ptychographical Lerative Engine)とFBP法(Filtered Back Projection)を用いて3次元X線タイコグラフィを実行するプログラム  

## X線タイコグラフィ
X線タイコグラフィとは，光学顕微鏡よりも高い空間分解能で試料観察を行う手法である．  
現在は，次世代放射光施設(東北大学の[NanoTerasu](https://www.pref.miyagi.jp/soshiki/shinsan/hosyakoshisaku.html)など)において，原子レベルの試料の観察に利用されている．   
X線タイコグラフィでは，試料にX線を照射することによって得られる回折光のデータから，コンピュータ上で位相回復計算と呼ばれるデータ処理を行い，試料像を再構成する．  

本プログラムでは，位相回復計算の代表例であるePIEをPythonで実装し，回折光のデータセットから2次元の試料像を再構成している．  
また，ePIEで得られた複数の2次元試料像から，FBP法を用いて3次元の試料像を生成を行った．

## 参考文献

Andrew M. Maiden, John M. Rodenburg, "An improved ptychographical phase retrieval algorithm for diffractive imaging," Ultramicroscopy, vol. 109, no. 10, Sep. 2009, pp. 1256-1262.
