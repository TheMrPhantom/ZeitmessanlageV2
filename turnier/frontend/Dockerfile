FROM node:20-alpine

# set working directory
WORKDIR /app

# add `/app/node_modules/.bin` to $PATH
ENV PATH=/app/node_modules/.bin:$PATH

# install app dependencies
COPY package.json ./
COPY package-lock.json ./
RUN npm install
RUN npm install react-scripts@3.4.1 -g
RUN npm install -g serve

# add app
COPY . ./

RUN npm run build 

# start app
CMD ["serve", "-s", "build", "-l", "80"]
