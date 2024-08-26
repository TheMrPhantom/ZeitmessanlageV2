FROM python:3.12-bookworm

WORKDIR /usr/src/app

#RUN apt update
#RUN apt -y install texlive texlive-luatex texlive-lang-german texlive-latex-extra pandoc

#RUN mkdir -p ~/.local/share/fonts
#COPY Latex/headline-font.ttf ./
#COPY Latex/text-font.ttf ./

#RUN cp ./headline-font.ttf ~/.local/share/fonts/
#RUN cp ./text-font.ttf ~/.local/share/fonts/

COPY requirements.txt ./
#RUN fc-cache -f -v
#RUN pip install -U pip
RUN pip install --no-cache-dir -r requirements.txt

COPY . .

CMD [ "python", "-u", "./main.py" ]