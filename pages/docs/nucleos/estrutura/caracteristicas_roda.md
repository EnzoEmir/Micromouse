# Características da Roda e Pneu do Micromouse

## 1. Introdução

Nesse documento, estão descritas as características centrais e demais informações referentes ao modelo de roda (que inclui a roda interna e o pneu) selecionado para o projeto de micromouse, que possuirá duas rodas na parte de trás da carcaça e um deslizador (skid) na frente.

As características definidas foram baseadas em diversos fatores, os quais estão, em sua maioria, descritos na página [*Esboço de características desejáveis das rodas*](pages/docs/nucleos/estrutura/esboco_caracteristicas_desejaveis_rodas.md). Além disso, a escolha também foi influenciada pelo fato de integrantes da equipe se disporem a emprestar rodas que possuiam, com propriedades adequadas, para o projeto. Essa abordagem permite que o grupo corte o custo de compra das rodas, e dispensa o tempo necessário para a encomenda e entrega do produto.

Em uma abordagem inicial, seria utilizado o conjunto roda interna ABS + pneu originais (Modelo 1.0 da roda). Em decisão posterior (Modelo 1.1), visando o encaixe com o eixo do motor, foi decidida que seria utilizado apenas o pneu emprestado, enquanto a roda interna seria impressa conforme CAD realizado.

<br>

## 2. Propriedades

### 2.1. Material

O pneu é composto de um **elastômero termoplástico (TPE)** chamado de **SBS** (*estireno-butadieno-estireno*). Esse material é conhecido pela sua flexibilidade, maciez e alta aderência, sendo essa última fundamental para o desejável alto atrito com o solo.

A roda interna, moldada e impressa para o encaixe com o pneu, é feita de filamento PLA (*Ácido Polilático*), um plástico biodegradável comumente utilizado em impressões 3D.

### 2.2. Dimensões

O pneu possui um diâmetro de *30 mm* e largura de *11 mm*. A roda interna, por sua vez, possui diâmetro de *17,75 mm* e largura de *7 mm*, além de um vão em formato de "D" no seu eixo de 
diamêtro *3,06 mm* e distância até a linha *2,6 mm*.

### 2.3. Textura da banda de rodagem

A textura da banda de rodagem é composta por uma linha central lisa com largura *1,94 mm*, e sulcos de comprimento *2,75 mm* em ambos os lados por toda a superfície.

Apesar de ser preferível uma superfície sem sulcos, para maximizar o atrito com a superfície lisa em *MDF*, a existência de sulcos não trará impactos negativos significantes para essa questão, dado o escopo e o contexto do projeto.

<br>

## 3. Desenhos Técnicos

Abaixo, estão apresentados os desenhos técnicos da roda interna e do pneu.

<p align="center"><i>Imagem 1:</i> Desenho técnico da Roda Interna</p>

![Roda Interna](../../../../mec/desenho%20tecnico/roda/Roda-1.1-Draft-Roda_01.png)

<p align="center"><b>Fonte: </b>Autoria de <a href="https://github.com/eduardodpms">Eduardo de Pina</a></p>

<br>

<p align="center"><i>Imagem 2:</i> Desenho técnico do Pneu</p>

![Pneu](../../../../mec/desenho%20tecnico/roda/Roda-1.0-Draft-Pneu_01.png)

<p align="center"><b>Fonte: </b>Autoria de <a href="https://github.com/eduardodpms">Eduardo de Pina</a></p>

<br>

## 4. Tabela de Versionamento

| Versão | Data | Autor | Descrição | Revisor |
| :-: | :-: | :-: | :-: | :-: |
| 1.0 | 12/05/2026 | [Eduardo de Pina](https://github.com/eduardodpms) | Criação do documento | [Giovanni Mateus](https://github.com/GiovanniMateus) |
| 1.1 | 02/06/2026 | [Eduardo de Pina](https://github.com/eduardodpms) | Adição de informações do ajuste da roda | [Giovanni Mateus](https://github.com/GiovanniMateus) |