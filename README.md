# 🚨Before the storm (폭풍전야)
모바일 스트레스 측정기를 이용한 Bio sync home 시스템

## 📌 프로젝트 개요
- 더 정확하고 신뢰도 높은 스트레스 측정   🚨
- (관련기사: https://www.mk.co.kr/news/society/10891941)
- 스트레스 수치에 따라 능동적으로 상태를 바꾸는 IoT Smart Home 구현을 통해 자신의 스트레스 수치를 인식하고, 위로 받을 수 있다.
  
## 📌 프로젝트 목표
- 심박 센서와, 온습도 센서로 받은 데이터를 통해 신뢰할 수 있는 스트레스 측정
- 스트레스 수치를 DB에 저장하고, 저장된 값에 따라 Bio sync 하는 smart home 구현 

## 📌 개발 요구사항
- 센서를 통해 얻은 데이터를 손실없이 DB에 저장해야 할 수 있다.
- 서버는 주기적으로 데이터를 수신받고, 스트레스 수치를 업데이트 할 수 있다.
- 서버는 업데이트 된 스트레스 수치에 따라 Home IoT의 상태를 바꿀 수 있다.

## 📌 사용 기술 및 개발환경
### 모바일 심박수, 온습도 측정기 (아두이노)
![bts_arduino](https://github.com/user-attachments/assets/d7ad8aac-84a6-43c1-aa8c-3bf327f37cc0)

### IoT 서버 (라즈베리 파이 4B)
[![Generic badge](https://img.shields.io/badge/Raspbian-Bookworm-red.svg)](https://shields.io/)
[![Generic badge](https://img.shields.io/badge/MariaDB-15.1-green.svg)](https://shields.io/)

### IoT 컨트롤러 (STM32 F411RE)
![bts_stmf411](https://github.com/user-attachments/assets/939b8757-04b6-4121-8f39-463907ad4b75)
