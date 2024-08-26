import subprocess
import smtplib
from email.mime.multipart import MIMEMultipart
from email.mime.base import MIMEBase
from email.mime.text import MIMEText
from email import encoders
import util


def mail_from_username(username):
    return username if "@" in username else f"{username}@{util.mail_postfix}"


def send_checkout_mails(user_infos):
    """
    {
        username: {balance,income,paid,name}
    }
    """
    for username, info in user_infos.items():
        mail = mail_from_username(username)
        # do for all
        send_checkout_mail(info["name"], info["balance"],
                           info["income"], info["paid"], mail)


def send_transfer_mails(from_name, from_alias, to_name, to_alias, from_message, to_message):
    from_mail = mail_from_username(from_name)
    to_mail = mail_from_username(to_name)

    if util.mail_server is None:
        return

    send_mail("Getränkelisten Überweisung", from_mail, f"""Hallo {from_alias if from_alias !='' else from_name},
{from_message}

Dein Getränkelisten Team
""")

    send_mail("Getränkelisten Überweisung", to_mail, f"""Hallo {to_alias if to_alias !='' else to_name},
{to_message}

Dein Getränkelisten Team
""")
    
def send_welcome_mail(username):
    mail=mail_from_username(username)
    send_mail("Wilkommen zur Drinklist",mail,f"""Hallo,
du wurdest zur Drinklist hinzugefügt. Du kannst ab jetzt auf {util.domain} Getränke abstreichen.
""")


def compile_latex(name):
    subprocess.run(["lualatex", name+".tex"], cwd="Latex")


def format_float(input):
    return '{0:.2f}'.format(input).replace(".", ",")


def send_mail(subject, to_address, body):
    print("Start sending mail")

    try:
        # Email details
        from_address = util.mail_email

        # Create the email
        msg = MIMEMultipart()
        msg["From"] = from_address
        msg["To"] = to_address
        msg["Subject"] = subject

        # Add the body of the email
        msg.attach(MIMEText(body, "plain"))

        # Connect to the email server
        server = smtplib.SMTP(util.mail_server, int(util.mail_port))
        server.starttls()
        # Login to the email server
        server.login(
            util.mail_username, util.mail_password)

        # Send the email
        server.sendmail(from_address, to_address, msg.as_string())

        # Disconnect from the server
        server.quit()
        print("Done sending mail")
    except:
        print("Failed to send to:", to_address)


def send_mail_with_attachment(subject, to_address, attachment, attachment_name, body):
    print("Start sending mail")
    # Email details
    from_address = util.mail_email

    # Create the email
    msg = MIMEMultipart()
    msg["From"] = from_address
    msg["To"] = to_address
    msg["Subject"] = subject

    # Add the body of the email
    msg.attach(MIMEText(body, "plain"))

    # Open the PDF file in binary mode
    with open(attachment, "rb") as attachment:
        # Add the PDF to the email as an attachment
        part = MIMEBase("application", "octet-stream")
        part.set_payload(attachment.read())

    # Encode the binary data into ASCII
    encoders.encode_base64(part)

    # Add header with PDF name
    part.add_header("Content-Disposition",
                    f"attachment; filename={attachment_name}")

    # Add the PDF attachment to the email
    msg.attach(part)

    # Connect to the email server
    server = smtplib.SMTP(util.mail_server, int(util.mail_port))
    server.starttls()
    # Login to the email server
    server.login(
        util.mail_username, util.mail_password)

    # Send the email
    server.sendmail(from_address, to_address, msg.as_string())

    # Disconnect from the server
    server.quit()
    print("Done sending mail")


def send_checkout_mail(name, current_balance, income, paid, mail_address):
    # income_sum = 0
    # paid_sum = 0

    # income_table = ""
    # paid_table = ""

    # for idx, i in enumerate(income):
    #     income_sum += i[2]

    #     line = ""
    #     if idx % 2 == 0:
    #         income_table += "\\rowcolor[gray]{.9}"

    #     line += f"{i[0]} & {i[1]} & {format_float(i[2])}€\\tabularnewline\n"
    #     income_table += line

    # for idx, i in enumerate(paid):
    #     paid_sum += i[2]

    #     line = ""
    #     if idx % 2 == 0:
    #         paid_table += "\\rowcolor[gray]{.9}"

    #     line += f"{i[0]} & {i[1]} & {format_float(i[2])}€\\tabularnewline\n"
    #     paid_table += line

    # paid_table += "\\midrule\n"
    # income_table += "\\midrule\n"

    # raw_latex_file = None
    # with open('Latex/checkout-template.tex') as reader:
    #     raw_latex_file = reader.read()

    # raw_latex_file = raw_latex_file.replace("??name??", name)
    # raw_latex_file = raw_latex_file.replace(
    #     "??current-balance??", format_float(current_balance))

    # raw_latex_file = raw_latex_file.replace(
    #     "??income-sum??", format_float(income_sum))
    # raw_latex_file = raw_latex_file.replace(
    #     "??paid-sum??", format_float(paid_sum))

    # raw_latex_file = raw_latex_file.replace("??income-table??", income_table)
    # raw_latex_file = raw_latex_file.replace("??paid-table??", paid_table)

    # filename = "checkout"

    # with open(f'Latex/{filename}.tex', "w") as writer:
    #     writer.write(raw_latex_file)

    # compile_latex(filename)

    send_mail("Getränkelisten Abrechnung", mail_address, util.checkout_mail_text.format(
        name=name, balance=format_float(current_balance)))
